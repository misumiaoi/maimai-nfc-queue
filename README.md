# maimai-nfc-queue

舞萌机厅排队的最小验证：读卡器读取交通联合卡卡号，墨水屏只显示后几位。刷一次加入队尾，已在队列中的卡再刷一次会提示确认；提示刷新完成、卡移开后，5 秒内再刷才退出。

持续贴卡只处理一次，需移开到连续 3 次检测不到卡后才能再次刷卡；不支持的卡同样只提示一次。墨水屏刷新仍会短暂阻塞读卡，请等刷新完成后移开再刷。

队列存在 ESP32 内部闪存里，断电后还在。不写卡，不读余额和乘车记录，不保存完整卡号。

如果闪存打开、盐值保存或队列读取/保存失败，会显示 `STORAGE ERROR` 并暂停排卡。运行中的保存失败会回退本次队列操作，不显示成功。请排查存储后重启；不要直接清空闪存，以免丢失原队列和设备盐值。

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

测试还用模拟时钟、屏幕、读卡事件和 NVS 验证确认窗口、主循环和存储故障处理。需要支持 AddressSanitizer / UndefinedBehaviorSanitizer 的 g++；若运行环境不支持 LeakSanitizer，可用 `ASAN_OPTIONS=detect_leaks=0 bash test/run.sh`，保留地址与未定义行为检查。

实体设备验收步骤见 [硬件测试清单](docs/hardware-checklist.md)。
