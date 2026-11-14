#pragma once

/**
 * 登记名字的输入口。板子没有键盘，名字在手机上填。
 *
 * ESP32 开一个不加密的热点。手机连上后打开网页提交名字，提交成功会调用 onName，
 * 参数是已经规范化的名字。取消调用 onCancel。网页收不到卡号，卡仍只由读卡器读取。
 *
 * 热点起不来时 begin 返回 false，串口登记仍然可用。
 */
class NamePortal {
 public:
  using NameHandler = void (*)(const char* normalizedName);
  using CancelHandler = void (*)();

  /**
   * 打开热点和网页。
   *
   * @return 热点已开始接受连接返回 true。
   */
  bool begin(NameHandler onName, CancelHandler onCancel);

  /** 处理 DNS 和网页请求，在 loop 里调用。 */
  void handle();

 private:
  bool up_ = false;
};
