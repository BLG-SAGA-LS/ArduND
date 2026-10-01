/*
 * ArduND : Ethernet Converter with Arduino Pro Micro (ETH-574) for HEIDENHAIN ND series
*/
#include <EEPROM.h>

// for Ethernet
#include <SPI.h>
#include <Ethernet.h>
byte ip[] = { 10, 77, 0, 208 };                       // dummy address
byte mac[] = { 0xFE, 0xFF, 0x00, 0x00, 0x00, 0x01 };  // dummy locally administered
EthernetServer server(7777);
EthernetClient client;

// for Serial Command Interpreter
String com, reply;

// for Ethernet Command Interpreter
String line, verb, terminators, ter, option;
byte address;
boolean assertEOI;

void setup() {
  // setup serial for setting parameters
  Serial.begin(9600);
  com = String();


  // setup serial1 for communication to ND
  Serial1.begin(9600, SERIAL_7E2);
  reply = String();

  // load ip address from EEPROM
  for (byte i = 0; i < 4; i++) ip[i] = EEPROM.read(i);  // IP address = 0 - 3

  // load mac address from EEPROM
  for (byte i = 4; i < 10; i++) mac[i - 4] = EEPROM.read(i);  // IP address = 4 - 9

  Ethernet.init(10);  // CS pin10
  Ethernet.begin(mac, IPAddress(ip[0], ip[1], ip[2], ip[3]));
  server.begin();
}

void loop() {
  // シリアル側コマンドインタプリタ
  // ? || ?HEL || HELP
  // ?MAC -> XX:XX:XX:XX:XX:XX
  // ?IPA -> XXX.XXX.XXX.XXX
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      com.trim();
      String v = com.substring(0, 4);
      v.toUpperCase();
      String o = com.substring(com.indexOf(' '));
      o.trim();
      if (com.equals("?") || v.equals("?HEL") || v.equals("HELP")) {
        Serial.print("? || ?HEL || HELP : to display this help.\r");
        Serial.print("?MAC : to display MAC address.\r");
        Serial.print("?IPA : to display IP address.\r");
        Serial.print("!MAC %02X:%02X:%02X:%02X:%02X:%02X : to set MAC address.\r ex. !MAC fe:ff:00:00:00:01\r");
        Serial.print("!IPA %d.%d.%d.%d : to set IP address.\r ex. !IPA 192.0.2.1\r");
        Serial.print("\n");
      } else if (v.equals("?MAC")) {
        char buff[8];
        for (int i = 0; i < 5; i++) {
          sprintf(buff, "%02x:", mac[i]);
          Serial.print(buff);
        }
        sprintf(buff, "%02x\r\n", mac[5]);
        Serial.print(buff);
      } else if (v.equals("?IPA")) {
        for (int i = 0; i < 3; i++) {
          Serial.print(ip[i], DEC);
          Serial.print('.');
        }
        Serial.print(ip[3], DEC);
        Serial.print("\r\n");
      } else if (v.equals("!MAC")) {
        o += ':';  // 末尾に':'を追加
        int j = 0;
        boolean isInvalid = false;
        char buf[3];
        long t[] = { -1, -1, -1, -1, -1, -1 };
        for (int i = o.indexOf(':'); i >= 0 && j < 6; i = o.indexOf(':'), j++) {
          (o.substring(0, i)).toCharArray(buf, 3);
          t[j] = strtol(buf, NULL, 16);
          isInvalid |= (t[j] < 0 || t[j] > 255);  // 0-255の範囲外ならInvalid
          o = o.substring(i + 1);
        }
        isInvalid |= (j != 6);  // 6つ読めなかった場合にInvalidとする
        if (!isInvalid) {
          for (int i = 0; i < 5; i++) {
            EEPROM.write(4 + i, (byte)t[i]);
            mac[i] = (byte)t[i];
            sprintf(buf, "%02X", t[i]);
            Serial.print(String(buf) + ":");
          }
          EEPROM.write(9, (byte)t[5]);
          mac[5] = (byte)t[5];
          sprintf(buf, "%02X", t[5]);
          Serial.print(String(buf) + " saved!\r\n");
        } else Serial.print("Invalid MAC address!\r\n");
      } else if (v.equals("!IPA")) {
        o += '.';  // 末尾に'.'を追加
        int j = 0;
        boolean isInvalid = false;
        long t[] = { -1, -1, -1, -1 };
        for (int i = o.indexOf('.'); i >= 0 && j < 4; i = o.indexOf('.'), j++) {
          t[j] = o.substring(0, i).toInt();
          isInvalid |= (t[j] < 0 || t[j] > 255);  // 0-255の範囲外ならInvalid
          o = o.substring(i + 1);
        }
        isInvalid |= (j != 4);  // 4つ読めなかった場合にInvalidとする
        if (!isInvalid) {
          for (int i = 0; i < 3; i++) {
            EEPROM.write(i, (byte)t[i]);
            ip[i] = (byte)t[i];
            Serial.print(t[i], DEC);
            Serial.print(".");
          }
          EEPROM.write(3, (byte)t[3]);
          ip[3] = (byte)t[3];
          Serial.print(t[3], DEC);
          Serial.print(" saved!\r\n");
        } else Serial.print("Invalid IP address!\r\n");
      } else Serial.print("Unknown command.\r\n");
      com = "";
    } else com += c;
  }

  // 新規Ethernet接続の管理
  EthernetClient new_client = server.accept();
  if (new_client) {         // 新しいクライアントが接続してきた
    if (client) {           // 既にクライアントが接続していたら
      new_client.println(readCurrentValue()); // 挨拶代わりに現在値を返して
      new_client.stop();    // 切断する
    } else {                // これが1つめのクライアントなら
      client = new_client;  // とりあえず受諾して
      client.println(readCurrentValue()); // 挨拶代わりに現在値を返す
      line = String();
    }
  }

  // Etherrnet側コマンドインタプリタ
  // ~ || BYE || QUIT || EXIT
  // ? || GET
  if (client && client.available()) {
    char c = client.read();
    if (c == '\n') {  // 終端文字なら
      line.trim();
      line.toUpperCase();
      // コマンドの解釈
      if (line.equals("~") || line.equals("BYE") || line.equals("QUIT") || line.equals("EXIT")) {  // クライアント停止
        client.stop();
      } else if (line.equals("?") || line.equals("GET")) {  // 現在値取得
        client.println(readCurrentValue());
      } else client.println("ERROR");  // 上記以外
      line = "";                       // バッファを空にする
    } else {
      line += String(c);
    }  // 終端じゃないなら
  }
  if (client && !client.connected()) client.stop();  // 切断されていたら解放する
}

String readCurrentValue(void) {
  for (int i = 0 ; i < 100 ; i++) { // リトライは最大100回
    String reply = String();
    Serial1.write(0x02); delay(100); // 取得コマンド送信
    while (Serial1.available() > 0) { // 溜まってるだけ読む
      char d = Serial1.read();
      reply += d;
    }
    reply.trim(); // トリムして
    if (reply.length() > 0) return reply; // 空行じゃなければリプライを返して脱出
    delay(50); // 空行だったなら50ミリ秒待ってリトライ    
  }
  return "ERROR"; // すべてのリトライに失敗したらエラーを返す
}