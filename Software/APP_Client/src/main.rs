// Prevent console window in addition to Slint window in Windows release builds when, e.g., starting the app via file manager. Ignored on other platforms.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use rumqttc::{AsyncClient, Event, Incoming, MqttOptions, QoS, Transport};
use rustls::ClientConfig;
use std::fs::File;
use std::io::{BufReader, ErrorKind};
use std::sync::{Arc, Mutex};
use std::time::Duration;

slint::include_modules!();

// ============================================================================
// 共享状态结构体：MQTT 后台线程写入，UI 主线程通过 Timer 轮询读取
// ============================================================================
struct MqttStatus {
    connected: bool,
    connection_status: String,
    broker_info: String,
    last_topic: String,
    last_message: String,
    message_count: i32,
    publish_count: i32,
    // 灯光控制状态
    light_on: bool,
    light_pending: bool,         // 等待确认中
    light_pending_value: bool,   // 等待确认的目标值
    // 温湿度数据
    temperature: String,
    humidity: String,
    sensor_topic: String,
}

// ============================================================================
// TLS 证书加载
// ============================================================================
fn load_ca_cert(ca_path: &str) -> ClientConfig {
    let mut root_cert_store = rustls::RootCertStore::empty();

    match File::open(ca_path) {
        Ok(ca_file) => {
            log::info!("从文件加载 CA 证书: {}", ca_path);
            let mut ca_buf_reader = BufReader::new(ca_file);
            let ca_certs: Vec<_> = rustls_pemfile::certs(&mut ca_buf_reader)
                .collect::<Result<Vec<_>, _>>()
                .expect("解析 CA 证书失败");
            for cert in ca_certs {
                root_cert_store
                    .add(cert)
                    .expect("添加 CA 证书到 RootCertStore 失败");
            }
        }
        Err(e) if e.kind() == ErrorKind::NotFound => {
            log::warn!("CA 证书文件 '{}' 未找到，将加载系统原生证书", ca_path);
            let native_certs =
                rustls_native_certs::load_native_certs().expect("无法加载系统原生证书");
            for cert in native_certs {
                let _ = root_cert_store.add(cert);
            }
            log::info!("已加载 {} 个系统原生证书", root_cert_store.len());
        }
        Err(e) => {
            panic!("打开 CA 证书文件失败: {}", e);
        }
    }

    ClientConfig::builder()
        .with_root_certificates(root_cert_store)
        .with_no_client_auth()
}

// ============================================================================
// main 函数：创建 Slint UI + tokio 运行时 + MQTT 后台任务
// ============================================================================
fn main() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .init();
    log::info!("=== MQTT 客户端启动 ===");

    // -----------------------------------------------------------------------
    // 1. 创建共享状态（MQTT 线程 ↔ UI 线程通信的桥梁）
    // -----------------------------------------------------------------------
    let mqtt_status = Arc::new(Mutex::new(MqttStatus {
        connected: false,
        connection_status: "正在连接...".to_string(),
        broker_info: "r07831cc.ala.cn-hangzhou.emqxsl.cn:8883".to_string(),
        last_topic: String::new(),
        last_message: String::new(),
        message_count: 0,
        publish_count: 0,
        light_on: false,
        light_pending: false,
        light_pending_value: false,
        temperature: "--".to_string(),
        humidity: "--".to_string(),
        sensor_topic: "test/sensor".to_string(),
    }));

    // -----------------------------------------------------------------------
    // 2. 创建 channel：Slint 回调 → MQTT 发布请求
    //    - tx (Sender)：在 Slint 按钮回调中发送
    //    - rx (Receiver)：在 MQTT 事件循环中接收并发布
    // -----------------------------------------------------------------------
    let (tx, rx) = tokio::sync::mpsc::channel::<(String, String)>(32);

    // 灯光控制 channel：Slint 回调 → MQTT 发布灯光指令
    let (light_tx, light_rx) = tokio::sync::mpsc::channel::<bool>(4);

    // -----------------------------------------------------------------------
    // 3. 创建 Slint UI
    // -----------------------------------------------------------------------
    let ui = AppWindow::new().unwrap();
    ui.set_is_connected(false);
    ui.set_connection_status("正在连接...".into());
    ui.set_broker_info("r07831cc.ala.cn-hangzhou.emqxsl.cn:8883".into());

    // -----------------------------------------------------------------------
    // 4. 注册 Slint 回调
    // -----------------------------------------------------------------------

    // 4a. 计数器按钮回调（保留原有功能）
    ui.on_request_increase_value({
        let ui_handle = ui.as_weak();
        move || {
            let ui = ui_handle.unwrap();
            ui.set_counter(ui.get_counter() + 1);
        }
    });

    // 4b. "发布到 MQTT" 按钮回调
    //     当用户点击按钮时，将主题和消息通过 channel 发送给 MQTT 后台任务
    ui.on_publish_clicked({
        let tx = tx.clone();
        let status = mqtt_status.clone();
        move |topic: slint::SharedString, message: slint::SharedString| {
            let topic = topic.to_string();
            let message = message.to_string();
            log::info!("UI 请求发布: 主题={}, 消息={}", topic, message);

            // 通过 channel 发送发布请求（try_send 非阻塞）
            match tx.try_send((topic, message)) {
                Ok(_) => {
                    // 更新发布计数
                    let mut s = status.lock().unwrap();
                    s.publish_count += 1;
                }
                Err(e) => {
                    log::error!("发送发布请求失败: {}", e);
                }
            }
        }
    });

    // 4c. 灯光开关切换回调
    //     当用户点击灯光开关时，通过 light_tx 发送请求给 MQTT 后台任务
    ui.on_light_toggled({
        let light_tx = light_tx.clone();
        let status = mqtt_status.clone();
        move |checked: bool| {
            log::info!("UI 请求灯光切换: {}", checked);
            // 设置等待确认状态
            {
                let mut s = status.lock().unwrap();
                s.light_pending = true;
                s.light_pending_value = checked;
            }
            // 发送灯光控制请求
            match light_tx.try_send(checked) {
                Ok(_) => {
                    log::info!("灯光切换请求已发送");
                }
                Err(e) => {
                    log::error!("发送灯光切换请求失败: {}", e);
                    let mut s = status.lock().unwrap();
                    s.light_pending = false;
                }
            }
        }
    });

    // -----------------------------------------------------------------------
    // 5. 启动定时器：每 200ms 轮询共享状态，更新 Slint UI
    //    这是 MQTT 后台线程 → UI 线程的数据流向
    // -----------------------------------------------------------------------
    let status_clone = mqtt_status.clone();
    let ui_weak = ui.as_weak();
    let _timer = slint::Timer::default();
    _timer.start(
        slint::TimerMode::Repeated,
        Duration::from_millis(200),
        move || {
            let status = status_clone.lock().unwrap();
            if let Some(ui) = ui_weak.upgrade() {
                ui.set_is_connected(status.connected);
                ui.set_connection_status(status.connection_status.as_str().into());
                ui.set_broker_info(status.broker_info.as_str().into());
                ui.set_last_topic(status.last_topic.as_str().into());
                ui.set_last_message(status.last_message.as_str().into());
                ui.set_message_count(status.message_count);
                ui.set_publish_count(status.publish_count);
                // 同步灯光状态到 UI
                ui.set_light_on(status.light_on);
                ui.set_light_pending(status.light_pending);
                // 同步温湿度到 UI
                ui.set_temperature(status.temperature.as_str().into());
                ui.set_humidity(status.humidity.as_str().into());
                ui.set_sensor_topic(status.sensor_topic.as_str().into());
            }
        },
    );

    // -----------------------------------------------------------------------
    // 6. 创建 tokio 多线程运行时，启动 MQTT 后台任务
    // -----------------------------------------------------------------------
    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .expect("创建 tokio 运行时失败");

    let status_for_mqtt = mqtt_status.clone();
    rt.spawn(async move {
        mqtt_event_loop(status_for_mqtt, rx, light_rx).await;
    });

    // -----------------------------------------------------------------------
    // 7. 运行 Slint 事件循环（主线程阻塞，直到窗口关闭）
    // -----------------------------------------------------------------------
    ui.run().unwrap();

    // 窗口关闭后清理
    drop(rt);
    log::info!("程序退出");
}

// ============================================================================
// MQTT 异步事件循环
//
// 架构说明：
// - tokio::select! 同时监听两个事件源：
//   1. eventloop.poll()：MQTT 服务器推送的消息（ConnAck/Publish/PingResp 等）
//   2. rx.recv()：来自 Slint UI 的发布请求（用户点击"发布到 MQTT"按钮）
// - 这样 Slint UI 操作和 MQTT 网络通信互不阻塞
// ============================================================================
async fn mqtt_event_loop(
    status: Arc<Mutex<MqttStatus>>,
    mut rx: tokio::sync::mpsc::Receiver<(String, String)>,
    mut light_rx: tokio::sync::mpsc::Receiver<bool>,
) {
    let broker = "r07831cc.ala.cn-hangzhou.emqxsl.cn";
    let port = 8883u16;
    let client_id = "rust-mqtt-client";
    let username = "app_client";
    let password = "123456aa";
    let broker_full = format!("{}:{}", broker, port);

    // 加载 TLS 配置
    let tls_config = load_ca_cert("./emqxsl-ca.crt");

    // 配置 MQTT
    let mut mqtt_options = MqttOptions::new(client_id, broker, port);
    mqtt_options.set_credentials(username, password);
    mqtt_options.set_transport(Transport::tls_with_config(tls_config.into()));
    mqtt_options.set_keep_alive(Duration::from_secs(5));

    log::info!("正在连接 MQTT Broker: {}", broker_full);

    // 更新状态：正在连接
    {
        let mut s = status.lock().unwrap();
        s.connected = false;
        s.connection_status = "正在连接...".to_string();
        s.broker_info = broker_full.clone();
    }

    // 创建异步客户端
    let (client, mut eventloop) = AsyncClient::new(mqtt_options, 10);

    // 订阅主题
    if let Err(e) = client.subscribe("test/topic", QoS::AtLeastOnce).await {
        log::error!("订阅失败: {:?}", e);
    } else {
        log::info!("已订阅主题: test/topic");
    }

    // 订阅灯光控制主题
    if let Err(e) = client.subscribe("test/light", QoS::AtLeastOnce).await {
        log::error!("订阅灯光主题失败: {:?}", e);
    } else {
        log::info!("已订阅主题: test/light");
    }

    // 订阅温湿度传感器主题
    if let Err(e) = client.subscribe("/topic/espcli/up", QoS::AtLeastOnce).await {
        log::error!("订阅传感器主题失败: {:?}", e);
    } else {
        log::info!("已订阅主题: /topic/espcli/up");
    }

    // 发布初始测试消息
    if let Err(e) = client
        .publish("test/topic", QoS::AtLeastOnce, false, "Hello TLS with Auth!")
        .await
    {
        log::error!("发布消息失败: {:?}", e);
    } else {
        log::info!("已发布初始测试消息到 test/topic");
    }

    // 处理事件循环：同时监听 MQTT 事件和 UI 发布请求
    log::info!("开始处理 MQTT 事件循环...");
    loop {
        tokio::select! {
            // 分支 1：处理 MQTT 服务器推送的事件
            notification = eventloop.poll() => {
                match notification {
                    Ok(event) => match event {
                        Event::Incoming(Incoming::ConnAck(conn_ack)) => {
                            log::info!("[MQTT] 连接结果: {:?}", conn_ack.code);
                            let mut s = status.lock().unwrap();
                            s.connected = true;
                            s.connection_status = format!("已连接 ({:?})", conn_ack.code);
                        }
                        Event::Incoming(Incoming::Publish(p)) => {
                            let payload = String::from_utf8_lossy(&p.payload);
                            log::info!("[MQTT] 收到消息: 主题={}, 内容={}", p.topic, payload);

                            if p.topic == "/topic/espcli/up" {
                                // 先尝试解析灯光控制消息: {light:0} 或 {light:1}
                                let light_val = parse_light_message(&payload);
                                if let Some(is_on) = light_val {
                                    log::info!("[MQTT] 灯光状态: {}", if is_on { "开" } else { "关" });
                                    let mut s = status.lock().unwrap();
                                    s.light_on = is_on;
                                    s.light_pending = false;  // 收到确认，清除等待状态
                                    s.last_topic = p.topic.clone();
                                    s.last_message = payload.to_string();
                                    s.message_count += 1;
                                } else {
                                    // 灯光解析失败，尝试解析温湿度传感器消息: {"temp":25.5,"humi":60.0}
                                    let sensor = parse_sensor_message(&payload);
                                    if let Some((temp, humi)) = sensor {
                                        log::info!("[MQTT] 传感器数据: 温度={}, 湿度={}", temp, humi);
                                        let mut s = status.lock().unwrap();
                                        s.temperature = temp;
                                        s.humidity = humi;
                                        s.sensor_topic = p.topic.clone();
                                        s.last_topic = p.topic.clone();
                                        s.last_message = payload.to_string();
                                        s.message_count += 1;
                                    } else {
                                        // 既不是灯光也不是传感器，作为普通消息处理
                                        let mut s = status.lock().unwrap();
                                        s.last_topic = p.topic.clone();
                                        s.last_message = payload.to_string();
                                        s.message_count += 1;
                                    }
                                }
                            } else {
                                let mut s = status.lock().unwrap();
                                s.message_count += 1;
                                s.last_topic = p.topic.clone();
                                s.last_message = payload.to_string();
                            }
                        }
                        Event::Incoming(Incoming::SubAck(sub_ack)) => {
                            log::info!("[MQTT] 订阅确认: {:?}", sub_ack);
                        }
                        Event::Incoming(Incoming::PubAck(pub_ack)) => {
                            log::info!("[MQTT] 发布确认: {:?}", pub_ack);
                        }
                        _ => {
                            log::debug!("[MQTT] 其他事件: {:?}", event);
                        }
                    },
                    Err(e) => {
                        log::error!("[MQTT] 连接错误: {:?}", e);
                        {
                            let mut s = status.lock().unwrap();
                            s.connected = false;
                            s.connection_status = format!("错误: {}", e);
                        }
                        log::info!("5秒后重试连接...");
                        tokio::time::sleep(Duration::from_secs(5)).await;
                        {
                            let mut s = status.lock().unwrap();
                            s.connection_status = "正在重连...".to_string();
                        }
                    }
                }
            }

            // 分支 2：处理来自 Slint UI 的发布请求
            // 当用户点击"发布到 MQTT"按钮时，channel 收到 (topic, message)
            Some((topic, message)) = rx.recv() => {
                log::info!("[UI→MQTT] 发布消息: 主题={}, 内容={}", topic, message);
                match client.publish(&topic, QoS::AtLeastOnce, false, message.as_bytes()).await {
                    Ok(_) => {
                        log::info!("[UI→MQTT] 发布成功");
                    }
                    Err(e) => {
                        log::error!("[UI→MQTT] 发布失败: {:?}", e);
                    }
                }
            }

            // 分支 3：处理来自 Slint UI 的灯光控制请求
            // 当用户点击灯光开关时，发送 {light:0} 或 {light:1}
            Some(light_on) = light_rx.recv() => {
                let light_msg = if light_on { "{\"light\":1}" } else { "{\"light\":0}" };
                log::info!("[UI→MQTT] 灯光控制: {}", light_msg);
                match client.publish("/topic/appcli/up", QoS::AtLeastOnce, false, light_msg.as_bytes()).await {
                    Ok(_) => {
                        log::info!("[UI→MQTT] 灯光控制消息发布成功");
                    }
                    Err(e) => {
                        log::error!("[UI→MQTT] 灯光控制消息发布失败: {:?}", e);
                        // 发布失败，清除等待状态
                        let mut s = status.lock().unwrap();
                        s.light_pending = false;
                    }
                }
            }
        }
    }
}

// ============================================================================
// 解析灯光消息: {light:0} 或 {light:1}
// ============================================================================
fn parse_light_message(payload: &str) -> Option<bool> {
    // 支持多种格式: {light:0}, {light:1}, {"light":0}, {"light":1}
    let trimmed = payload.trim();
    if trimmed.starts_with('{') && trimmed.ends_with('}') {
        let inner = &trimmed[1..trimmed.len() - 1];
        let parts: Vec<&str> = inner.split(':').collect();
        if parts.len() == 2 {
            let key = parts[0].trim().trim_matches('"');
            let val = parts[1].trim().trim_matches('"');
            if key == "light" {
                return match val {
                    "1" => Some(true),
                    "0" => Some(false),
                    _ => None,
                };
            }
        }
    }
    None
}

// ============================================================================
// 解析温湿度传感器消息: {temp:25.5,humi:60.0}
// 支持格式: {temp:25.5,humi:60.0}, {"temp":25.5,"humi":60.0}
// ============================================================================
fn parse_sensor_message(payload: &str) -> Option<(String, String)> {
    let trimmed = payload.trim();
    if trimmed.starts_with('{') && trimmed.ends_with('}') {
        let inner = &trimmed[1..trimmed.len() - 1];
        let mut temp: Option<String> = None;
        let mut humi: Option<String> = None;

        for part in inner.split(',') {
            let kv: Vec<&str> = part.split(':').collect();
            if kv.len() == 2 {
                let key = kv[0].trim().trim_matches('"');
                let val = kv[1].trim().trim_matches('"');
                match key {
                    "temp" => temp = Some(val.to_string()),
                    "humi" => humi = Some(val.to_string()),
                    _ => {}
                }
            }
        }

        if temp.is_some() || humi.is_some() {
            return Some((
                temp.unwrap_or_else(|| "--".to_string()),
                humi.unwrap_or_else(|| "--".to_string()),
            ));
        }
    }
    None
}
