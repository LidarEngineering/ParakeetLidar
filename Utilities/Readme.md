This folder contains the utilities used to support the Parakeet Lidar from Lidar Engineering LLC.

BlueScan  -- A windows based installer for the BlueScan3D viewer.  This viewer can be used to visually monitor the operation of a Parakeet Lidar. 
             You can also change the settings, upgrade the firmware, and record and playback scans from the sensor.
             
UserManual-- Manual discribing the Parakeet Lidar Specification, visualization software, and communications protocol.
          
Upgrade   -- A linux based CLI utility to upgrade the Parakeet Lidar with new Firmware.  The various firmware upgrades are available in the Firmware folder.
             Under Linux and GCC use the following command line  ##>gcc upgrade.c -o upgrade
               Example Command Line
                   ##>upgrade -d 192.168.0.5 -p 6543 -f LDS-50C-C30E_2022-09-18.lhl
                    Where:     -d	Lidar IP Address      ie. 192.168.0.5
                               -p Destination IP Port   ie. 6543
                               -f	Firmware File Name    ie. LDS-50C-C30E_2022-09-18.lhl


