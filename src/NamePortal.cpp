#include "NamePortal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdio.h>

#include "NameRegistry.h"
#include "config.h"

namespace {

constexpr int kPageCap = 1024;

DNSServer g_dns;
WebServer g_http(80);
NamePortal::NameHandler g_onName = nullptr;
NamePortal::CancelHandler g_onCancel = nullptr;

const char kHead[] =
    "<!DOCTYPE html><html lang=zh><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>body{font:18px sans-serif;margin:24px;max-width:28em}"
    "input,button{font-size:20px}input{width:100%;padding:8px;box-sizing:border-box}"
    "button{margin-top:12px;padding:10px 16px}</style>";

const char kForm[] =
    "<h1>绑定名字</h1>"
    "<p>先填名字，再在 60 秒内把交通联合卡贴到读卡器上。这一下只绑定，不会入队。"
    "墨水屏只能显示英文和数字，最长 12 个字符，可以用空格和连字符。</p>"
    "<form method=post action=/bind>"
    "<input name=name maxlength=32 autocomplete=nickname required autofocus>"
    "<button type=submit>提交，然后去贴卡</button></form>";

void sendHtml(int code, const char* body) {
  char page[kPageCap];
  int n = snprintf(page, sizeof(page), "%s%s", kHead, body);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(page)) {
    g_http.send(500, "text/plain; charset=utf-8", "page too long");
    return;
  }
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(code, "text/html; charset=utf-8", page);
}

void sendForm() { sendHtml(200, kForm); }

void handleBind() {
  if (!g_http.hasArg("name") || g_http.arg("name").length() > 64) {
    sendHtml(400,
             "<h1>名字无效</h1><p>请用英文字母、数字、空格或连字符，1 到 12 个字符。</p>"
             "<p><a href=/>返回</a></p>");
    return;
  }

  char name[kMaxNameLen + 1];
  if (!normalizeName(g_http.arg("name").c_str(), name, sizeof(name))) {
    sendHtml(400,
             "<h1>名字无效</h1>"
             "<p>请用英文字母、数字、空格或连字符，1 到 12 个字符，"
             "不要以连字符开头或结尾。</p><p><a href=/>返回</a></p>");
    return;
  }

  if (g_onName != nullptr) g_onName(name);

  char body[384];
  snprintf(body, sizeof(body),
           "<h1>请贴卡</h1><p>60 秒内把交通联合卡贴到读卡器上，绑定 <b>%s</b>。"
           "这一下只绑定名字，不会入队。</p>"
           "<form method=post action=/cancel><button type=submit>取消</button></form>",
           name);
  sendHtml(200, body);
}

void handleCancel() {
  if (g_onCancel != nullptr) g_onCancel();
  sendHtml(200, "<h1>已取消</h1><p>这次没有绑定。<a href=/>重新填写</a></p>");
}

}  // namespace

bool NamePortal::begin(NameHandler onName, CancelHandler onCancel) {
  g_onName = onName;
  g_onCancel = onCancel;
  up_ = false;

  const IPAddress ip(192, 168, 4, 1);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(config::kWifiSsid)) {
    Serial.println("ERR wifi ap");
    return false;
  }

  g_dns.start(53, "*", ip);
  g_http.on("/bind", HTTP_POST, handleBind);
  g_http.on("/cancel", HTTP_POST, handleCancel);
  g_http.onNotFound(sendForm);
  g_http.begin();

  up_ = true;
  Serial.print("register: http://");
  Serial.print(ip);
  Serial.print("  wifi ");
  Serial.println(config::kWifiSsid);
  return true;
}

void NamePortal::handle() {
  if (!up_) return;
  g_dns.processNextRequest();
  g_http.handleClient();
}
