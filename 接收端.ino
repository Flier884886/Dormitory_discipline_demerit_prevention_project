//接收端代码
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>   //使用WiFiMulti库 
#include <Arduino.h>
#include <SPI.h>                //提供SPI通信支持
#include <SdFat.h>              //使用比默认SD库更强大的SdFat库
#include <painlessMesh.h>       //用于mesh组网
#include <cmath>
#include <complex>
#include <ctime>
#include <string>
#include <array>
#include <vector>
#include <list>
#include <algorithm>
#include <numeric>
#include <functional>
#include <limits.h>
using std::vector;
using std::list;

/*常量区*/
const bool Debug=false;
const int Pin[]={16,5,4,0,2,14,12,13,15};
const int chipSelect =Pin[8];//SD卡片选(CS)引脚
const int LEDL=Pin[1];
const int LEDR=Pin[2];
const int buzzer=Pin[3];
const int LED=Pin[4];
const int switcher=Pin[0];
const int mainlooptimes=20;
const String record_file_name="/record.txt";//记录文件路径
const String MESH_PREFIX="MasterChen08";// 设置当前MESH网络的名称
const String MESH_PASSWORD="ChenMou123";// MESH密码
const long ValidTime=15000;//消息时效ms
const int MESH_PORT=3453;// MESH端口
const String unit="A";//楼栋号
const int location=7;//安装位点
const int floornum=5;//楼层
const String name="陈师傅";//为每个终端起个名

// ADC 阈值定义（需根据实际测量校准），用于判断亮度函数
const int Brightness=500;
/*常量区结束*/

/*参数区*/
bool EnableSD=false;//指示SD卡是否可用
/*参数区结束*/
SdFat sd;//创建SD对象以支持SD卡访问
painlessMesh  mesh;//创建MESH对象
Scheduler userScheduler;//创建调度器供mesh
struct sensordata{
  int location;
  int L;
  int R;
  String name;
  String unit;
  int floor;
  long upgradetime;
};
list<sensordata> sensors;

/*---------------------------------------------------------------------MESH相关函数Start*/
void receivedCallback( uint32_t from, String &msg ) { // 收到消息的时候，执行该回调方法，打印收到的消息
  //Serial.printf("control server: Received from %u msg=%s\n", from, msg.c_str());
  // 1. 创建一个JSON文档（StaticJsonDocument），并指定其容量（单位是字节）
  StaticJsonDocument<1024> doc; 

  // 2. 将JSON字符串反序列化到文档中
  DeserializationError error = deserializeJson(doc, msg.c_str());

  // 3. 检查是否解析成功
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // 4. 从文档中提取数据
  String msgtype = doc["type"];
  String msgname=doc["name"];
  String msgunit=doc["unit"];
  int msgfloor=doc["floor"];
  int msglocation=doc["location"];
  int msgbright=doc["bright"];
  bool msgL=doc["left"].as<bool>();
  bool msgR=doc["right"].as<bool>();
  if(msgtype=="people" && msgunit==unit && msgfloor==floornum){
    bool exist=false;
    for(auto &a:sensors){
      if(a.location==msglocation){
        exist=true;
        a.L=msgL;
        a.R=msgR;
        a.upgradetime=millis();
      }
    }
    if(!exist){
      struct sensordata a;
      a.location=msglocation;
      a.L=msgL;
      a.R=msgR;
      a.upgradetime=millis();
      sensors.push_back(a);
    }
  }
}

void SendPeopleMsg(bool left,bool right) {
    // 以json的形式进行包装数据
    StaticJsonDocument<1024> doc;//创建json文档，文档大小1024字节
    //向文档中添加数据
    doc["type"] = "people";
    doc["name"] = name;
    doc["unit"] = unit;
    doc["floor"] = floornum;
    doc["location"] = location;
    doc["bright"] = analogRead(A0);//发送亮度信息
    doc["left"] = (int)left;
    doc["right"] = (int)right;

    //将文档序列化并输出到String
    String str;
    serializeJson(doc, str);
    mesh.sendBroadcast(str);  // 发送广播消息

}

/*---------------------------------------------------------------------MESH相关函数End*/

/*---------------------------------------------------------------------存储相关函数Start--------------------------------------------------------*/


String DoubleToString(double value, int precision = 10) {
  // 处理特殊情况：NaN和无穷大
  if (isnan(value)) return "NaN";
  if (isinf(value)) return value < 0 ? "-Inf" : "Inf";
  // 使用String的构造方法，直接控制精度
  return String(value, precision);
}

double StringToDouble(const String& str) {
  // 检查字符串是否为空或全为空白字符
  if (str.length() == 0) {
    return 0.0;
  }
  String trimmedStr = str;
  trimmedStr.trim(); // 去除首尾空白字符
  if (trimmedStr.length() == 0) {
    return 0.0;
  }
  // 使用String类的toDouble方法进行转换，该方法内置了错误处理
  char* endptr;
  const char* cstr = trimmedStr.c_str();
  double result = strtod(cstr, &endptr);
  // 检查整个字符串是否被成功转换
  if (endptr == cstr || *endptr != '\0') {
    // 转换失败，返回0.0
    return 0.0;
  }
  return result;
}

/*---------------------------------------------SD卡函数Start*/

bool SDinit(int retries = 1) {
  // 首先检查SD卡是否已经初始化成功
  if (sd.begin(0)) { // 使用0作为参数可以检测而不实际初始化
    EnableSD=true;
    return true;
  }
  Serial.println("尝试初始化SD卡...");
  // 重试初始化
  for (int attempt = 1; attempt <= retries; attempt++) {
    Serial.print("初始化尝试 #");
    Serial.println(attempt);
    // 执行SD卡初始化
    if (sd.begin(chipSelect)) {
      Serial.println("SD卡初始化成功");
      // 验证文件系统可访问性
      if (sd.exists("/")) {
        Serial.println("SD卡文件系统验证成功。");
				EnableSD=true;
        return true;
      } else {
        Serial.println("警告: 初始化成功但文件系统无法访问。");
      }
    }
    // 如果未达到最大重试次数，延迟后重试
    if (attempt < retries) {
      Serial.println("初始化失败,将在0.5秒后重试...");
      delay(500);
    }
  }
  
  // 所有重试都失败
  Serial.println("错误: SD卡初始化失败,已达到最大重试次数");
  return false;
}


//在SD卡创建并写入文本文件(原文件存在则清空)
bool SDnewtextfile(String text,String path){
	FsFile myFile; // 此File是SdFat库定义的，与标准库不冲突
	// 写入文件：类似于 ofstream，使用 ios::out 模式
  // 这会创建文件（如果不存在）并清空现有内容
  myFile = sd.open(path, O_WRITE | O_CREAT | O_TRUNC);
  if (myFile) {
    myFile.println(text); // 像cout一样使用println
    myFile.close(); // 关闭文件
		return true;
  }else{
		return false;
	}
}
//在SD卡追加文本文件(追加一行)
bool SDappendtextfile(String text,String path){
	if (!sd.exists(path.c_str())) {//文件不存在，新建
    Serial.println("创建新文件...");
    SdFile myFile;
    if (myFile.open(path.c_str(), O_WRITE | O_CREAT)) {
      myFile.println(text);
      myFile.close();
		return true;
    }else{
			return false;
		}
  } else {//文件存在
    FsFile myFile; // 此File是SdFat库定义的，与标准库不冲突
		//追加模式：类似于 ofstream 使用 ios::app
  	myFile = sd.open(path, O_WRITE | O_APPEND);
  	if (myFile) {
  	  myFile.println(text);
  	  myFile.close();
			return true;
  	}else{
			return false;
		}
  }
	
}
//在SD卡读取文本文件
String SDreadtextfile(String path){
  String content = ""; // 创建一个空字符串用于存储内容
  FsFile file = sd.open(path, O_READ); // 以只读模式打开文件
  // 检查文件是否成功打开
  if (!file) {
    Serial.print("错误：无法打开文件(读取)");
    Serial.println(path);
    return content; // 返回空字符串
  }
  //用readString读取从当前位置到文件末尾的所有内容()简单直接)
  content = file.readString();
  file.close(); // 操作完成，关闭文件
  Serial.println("文件读取完成。");
  return content;
}

/*---------------------------------------------SD卡函数End*/
/*---------------------------------------------------------------------存储相关函数End--------------------------------------------------------*/

//获取指定数组最大值的下标
int findMaxIndex(vector<int> arr) {
  if (arr.size() <= 0) return -1; // 处理空数组
  int max_index = 0;
  for (int i = 1; i < arr.size(); i++) {
    if (arr[i] > arr[max_index]) {
      max_index = i;
    }
  }
  return max_index;
}

//获取亮度布尔值(超过阈值则为true)
bool GetBrightness(){
	int Value = analogRead(A0); // 读取A0引脚，获得0-1023的数值
	if(Value<Brightness)return false;
	else return true;
}


void setup(){
	Serial.begin(9600);//启用串口通信
	//初始化引脚
	pinMode(buzzer, OUTPUT);
	pinMode(LED, OUTPUT);
	pinMode(LEDL, OUTPUT);
	pinMode(LEDR, OUTPUT);
	pinMode(switcher, OUTPUT);

		/*SD卡首次初始化过程*/
		// 初始化SPI并设置引脚
  	// 注意：SPI库通常会使用ESP8266的硬件SPI默认引脚（GPIO14=D5(SCK), GPIO12=D6(MISO), GPIO13=D7(MOSI)）
  	// 只需要单独指定CS引脚（D8）
		pinMode(chipSelect, OUTPUT);
  	digitalWrite(chipSelect, HIGH);//将片选引脚设置为高电平(不选中从设备)(抛弃了上拉电阻方案,会导致启动失败)
  	SPI.begin(); // 初始化SPI总线
		digitalWrite(chipSelect, LOW);//将片选引脚设置为低电平(选中设备)
		SDinit();//尝试初始化SD卡(最高耗时0.5s)
		/*SD卡首次初始化过程结束*/
		if(EnableSD){
		std::stringstream ss;
		ss <<"Started.---------------------------------------";
		std::string text=ss.str();
		SDappendtextfile(text.c_str(),record_file_name);
	}

  Serial.println("准备MESH网络······");
  if(Debug) mesh.setDebugMsgTypes( ERROR | CONNECTION | S_TIME ); // 设置需要展示的debug信息(打印到串口监视器)
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA, 6 ); // 初始化mesh网络

  mesh.onNewConnection([](size_t nodeId) {          // 新节点接入的时候
    Serial.printf("New Connection %u\n", nodeId);   // 打印节点唯一id
  });

  mesh.onDroppedConnection([](size_t nodeId) {        // 节点连接丢失的时候
    Serial.printf("Dropped Connection %u\n", nodeId); // 打印丢失的节点id
  });

  mesh.onReceive(&receivedCallback);                  // 收到消息的时候，执行receivedCallback()
	Serial.println("MESH初始化完成");
  
  digitalWrite(buzzer, LOW);
	Serial.println("启动成功");
}

void loop(){
	bool people=false;
	for(int i=0;i<mainlooptimes;i++){//高频操作写在循环体内
    long S=millis();
    while(millis()-S < 100){//等待期间做这些
      mesh.update(); // 保持Mesh网络运行
      bool TO=false;
		  for(auto &a:sensors){
        if(millis()-a.upgradetime>2000){//信息不在2s内更新
          TO=true;
          break;
        }
        mesh.update();
      }
      mesh.update();
      if(TO){
        for(int n=0;n<5;n++){
          for(int i=1;i<2000;i++){//闪烁
            digitalWrite(buzzer,HIGH);
            mesh.update();
          }
          for(int i=1;i<2000;i++){//闪烁
            digitalWrite(buzzer,LOW);
            mesh.update();
          }
        }
      }
    }
    mesh.update();
    S=millis();
    bool mL=false;
    bool mR=false;
    bool HaveTimeOut=false;
    bool InfCanUse=false;
		for(auto &a:sensors){
      if(millis()-a.upgradetime<=ValidTime){//信息在有效时间内
        if(a.L) mL=true;
        if(a.R) mR=true;
        if(a.location>=location-1 || a.location<=location+1) InfCanUse=true;
      }else{
        HaveTimeOut=true;
      }
      mesh.update();
    }
    mesh.update();
    //亮灯提示
    if(mL){
      digitalWrite(LEDL,HIGH);
    }else{
      digitalWrite(LEDL,LOW);
    }
    if(mR){
      digitalWrite(LEDR,HIGH);
    }else{
      digitalWrite(LEDR,LOW);
    }
    if(mR || mL){
      digitalWrite(LED,LOW);
      people=true;
    }else{
      digitalWrite(LED,HIGH);
    }
    if(InfCanUse){
      digitalWrite(switcher,LOW);
    }else{
      digitalWrite(switcher,HIGH);
    }
    mesh.update();
    if(HaveTimeOut){//存在过时消息
      mesh.update();
      sensors.remove_if([](struct sensordata a) {
        return millis()-a.upgradetime > ValidTime; // 定义删除条件：如果过时则删除
      });
      mesh.update();
    }
	}
  if(EnableSD && people){
		std::stringstream ss;
		ss << millis();
		std::string text=ss.str();
		SDappendtextfile(text.c_str(),record_file_name);
		Serial.println("SD卡写入记录");
	}
}
