This folder contains the utilities used to support the Parakeet Lidar from Lidar Engineering LLC.

Parakeet  -- A windows based program for exercising and viewing scan data from the Parakeet Lidar. This executable file can be placed in a folder and executed directly.  There is no installation.
               Version 1.2.2.5 is the current version.


LidarView -- A windows based program for configuring and testing the Parkaet Zone Controller.  This executagle is a windows installer.  Exeuting this file will cause the LidarView program to be
               installed.  Version 1.7.3.4 is the current version.
             
Upgrade   -- A linux based CLI utility to upgrade the Parakeet Lidar with new Firmware.  The various firmware upgrades are available in the Firmware folder.
             Under Linux and GCC use the following command line  ##>gcc upgrade.c -o upgrade
               Example Command Line
                   ##>upgrade -d 192.168.0.5 -p 6543 -f LDS-50C-C30E_2022-09-18.lhl
                    Where:     -d	Lidar IP Address      ie. 192.168.0.5
                               -p Destination IP Port   ie. 6543
                               -f	Firmware File Name    ie. LDS-50C-C30E_2022-09-18.lhl


