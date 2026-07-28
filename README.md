# LakiBeam C++ SDK

LakiBeam C++ SDK 用于接收 LakiBeam 激光雷达采集的数据，并提供雷达参数配置接口。

## 项目简介

LakiBeam 数据采集系统由以下部分组成：

- **LakiBeam**：采集环境数据和信息。
- **Ethernet**：通过以太网连接设备端，雷达数据通过 UDP 协议发送。
- **SDK**：提供数据接收、雷达配置、IP 地址配置等接口。

## 环境要求

SDK 以源码形式发布，需要集成到目标系统。

依赖：

- C++ 编译环境
- Boost 库（推荐 1.63 及以上版本）

支持环境：

- Windows + Visual Studio 2019
- Ubuntu + Visual Studio Code

## Windows 编译

1. 安装 Visual Studio 2019。
2. 安装 Boost 库。
3. 在项目属性中配置：
   - `C/C++` → 附加包含目录
   - `链接器` → 附加库目录
4. 添加 Boost 依赖路径。
5. 创建源文件。
6. 添加 SDK 源码。
7. 编译运行。

## Ubuntu 编译

安装 Boost：

```bash
cd boost_1_78_0
./bootstrap.sh
./b2 install --prefix=/home/user/boost

cd ~/boost

sudo mv -f ./lib/* /usr/lib
sudo cp -rf ./include/boost /usr/include
```

获取 SDK：

```bash
sudo apt-get install rar
sudo apt-get install unrar
```

使用 VS Code 打开 SDK 工程并编译运行。

## UDP 接口

### LakiBeamUDP

用于接收 LakiBeam 雷达 UDP 数据，并重新组装深度数据。

#### 构造函数

```cpp
LakiBeamUDP(
    string localIP,
    string localPort,
    string laserIP,
    string laserPort
);
```

#### 参数

| 参数 | 说明 |
| --- | --- |
| `localIP` | 本地 IP |
| `localPort` | 本地端口 | 
| `laserIP` | 雷达 IP |
| `laserPort` | 雷达端口 |

#### 函数

| 函数 | 说明 |
| --- | --- |
| `dots_valid()` | 获取有效雷达数据数量 |
| `get_pack()` | 获取雷达数据包 |
| `restart()` | 重启 UDP SDK 功能 |
| `wait()` | 等待下一包数据 |

## HTTP 接口

### LakiBeamHTTP

用于配置雷达运行参数。

支持：

- 获取固件信息
- 获取网络信息
- 获取扫描参数
- 设置扫描频率
- 设置激光状态
- 设置 Host IP 和端口
- 系统复位

### 获取接口

| 函数 | 功能 |
| --- | --- |
| `delete_override()` | 删除静态模式配置并设置为 DHCP 模式 |
| `get_filter_level()` | 获取滤波等级 |
| `get_firmware()` | 获取固件信息 |
| `get_host()` | 获取 Host 配置 |
| `get_host_IP()` | 获取 Host IP |
| `get_host_port()` | 获取 Host 端口 |
| `get_laser_enable()` | 获取激光状态|
| `get_laser_start()` | 获取激光扫描起始角度|
| `get_lase_stop()` | 获取激光扫描结束角度|
| `get_monitor()` | 获取系统监控信息 |
| `get_motor_rpm()` | 获取雷达实时转速 |
| `get_network()` | 获取网络信息 |
| `get_overview()` | 获取雷达实时状态信息 |
| `get_resolution()` | 获取角分辨率 |
| `get_scan_range()` | 获取扫描范围 |
| `get_scanfreq()` | 获取扫描频率 |

### 设置接口

| 函数 | 功能 |
| --- | --- |
| `put_filter_level()` | 设置滤波等级 |
| `put_host_IP()` | 设置 Host IP |
| `put_host_port()` | 设置 Host 端口 |
| `put_scanfreq()` | 设置扫描频率 |
| `put_laser_enable()` | 设置激光状态 |
| `put_laser_start()` | 设置扫描起始角度 |
| `put_laser_stop()` | 设置扫描结束角度 |
| `put_override()` | 设置静态 IP 覆盖值 |
| `put_reset()` | 系统复位 |


## SDK 开发流程

雷达默认采用主动上传数据模式。

1. 获取雷达配置中的 `HostIP` 和 `DataPort`。
2. 设置网卡 IP 与 `HostIP` 保持一致。
3. 根据 `DataPort` 创建数据接收端口。
4. 连接雷达并获取数据。
5. 根据需要修改雷达参数。
6. 循环读取数据。

## License

Copyright © LakiBeam
