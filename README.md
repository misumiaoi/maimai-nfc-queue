# maimai-nfc-queue

舞萌机厅排队的最小验证：读卡器读取交通联合卡卡号，墨水屏只显示后几位。刷一次加入队尾，已在队列中的卡再刷一次会提示确认，5 秒内再刷才退出。

队列存在 ESP32 内部闪存里，断电后还在。不写卡，不读余额和乘车记录，不保存完整卡号。

## 硬件

- ESP32
- PN532（I2C，模块拨码拨到 I2C）
- 4.2 寸黑白墨水屏（Waveshare 4.2" V2 / GoodDisplay GDEY042T81）
- 按钮接 GPIO27 到 GND，长按 3 秒移除队首

接线见 `src/config.h`。旧批次墨水屏把 `src/QueueDisplay.cpp` 里的驱动换成 `GxEPD2_420`。

## 构建

```
pio run
pio run -t upload
```

不依赖硬件的卡号解析和队列逻辑可以在电脑上测：

```
test/run.sh
```
