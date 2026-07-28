# LakiBeam C++ SDK

The LakiBeam C++ SDK is used to receive data collected by LakiBeam lidar devices and provides interfaces for configuring lidar parameters.

## Project Overview

The LakiBeam data acquisition system consists of the following components:

- **LakiBeam**: Collects environmental data and information.
- **Ethernet**: Connects the lidar to the host device through Ethernet. Lidar data is transmitted via the UDP protocol.
- **SDK**: Provides interfaces for receiving data, configuring the lidar, and configuring IP addresses.

## Environment Requirements

The SDK is released as source code and must be integrated into the target system.

Dependencies:

- C++ development environment
- Boost library, version 1.63 or later recommended

Supported environments:

- Windows + Visual Studio 2019
- Ubuntu + Visual Studio Code

## Windows Build

1. Install Visual Studio 2019.
2. Install the Boost library.
3. Configure the following items in the project properties:
   - `C/C++` → Additional Include Directories
   - `Linker` → Additional Library Directories
4. Add the Boost dependency paths.
5. Create a source file.
6. Add the SDK source code.
7. Build and run the project.

## Ubuntu Build

Install Boost:

```bash
cd boost_1_78_0
./bootstrap.sh
./b2 install --prefix=/home/user/boost

cd ~/boost

sudo mv -f ./lib/* /usr/lib
sudo cp -rf ./include/boost /usr/include
```

Obtain the SDK:

```bash
sudo apt-get install rar
sudo apt-get install unrar
```

Open the SDK project in VS Code, then build and run it.

## UDP API

### LakiBeamUDP

Used to receive LakiBeam lidar UDP data and reconstruct depth data.

#### Constructor

```cpp
LakiBeamUDP(
    string localIP,
    string localPort,
    string laserIP,
    string laserPort
);
```

#### Parameters

| Parameter | Description |
| --- | --- |
| `localIP` | Local IP address |
| `localPort` | Local port |
| `laserIP` | Lidar IP address |
| `laserPort` | Lidar port |

#### Functions

| Function | Description |
| --- | --- |
| `dots_valid()` | Gets the number of valid lidar data points |
| `get_pack()` | Gets a lidar data packet |
| `restart()` | Restarts the UDP SDK function |
| `wait()` | Waits for the next data packet |

## HTTP API

### LakiBeamHTTP

Used to configure lidar operating parameters.

Supported operations:

- Get firmware information
- Get network information
- Get scan parameters
- Set the scan frequency
- Set the laser state
- Set the Host IP address and port
- Reset the system

### Getter Interfaces

| Function | Description |
| --- | --- |
| `delete_override()` | Deletes the static-mode configuration and switches to DHCP mode |
| `get_filter_level()` | Gets the filter level |
| `get_firmware()` | Gets firmware information |
| `get_host()` | Gets the Host configuration |
| `get_host_IP()` | Gets the Host IP address |
| `get_host_port()` | Gets the Host port |
| `get_laser_enable()` | Gets the laser state |
| `get_laser_start()` | Gets the laser scan start angle |
| `get_lase_stop()` | Gets the laser scan end angle |
| `get_monitor()` | Gets system monitoring information |
| `get_motor_rpm()` | Gets the lidar real-time rotational speed |
| `get_network()` | Gets network information |
| `get_overview()` | Gets the lidar real-time status information |
| `get_resolution()` | Gets the angular resolution |
| `get_scan_range()` | Gets the scan range |
| `get_scanfreq()` | Gets the scan frequency |

### Setter Interfaces

| Function | Description |
| --- | --- |
| `put_filter_level()` | Sets the filter level |
| `put_host_IP()` | Sets the Host IP address |
| `put_host_port()` | Sets the Host port |
| `put_scanfreq()` | Sets the scan frequency |
| `put_laser_enable()` | Sets the laser state |
| `put_laser_start()` | Sets the scan start angle |
| `put_laser_stop()` | Sets the scan end angle |
| `put_override()` | Sets the static IP override value |
| `put_reset()` | Resets the system |

## SDK Development Process

The lidar uses active upload mode by default.

1. Obtain `HostIP` and `DataPort` from the lidar configuration.
2. Set the network adapter IP to match `HostIP`.
3. Create a data receiving port according to `DataPort`.
4. Connect to the lidar and acquire data.
5. Modify the lidar parameters as required.
6. Poll and read the data continuously.

## License

Copyright © LakiBeam
