/*************************************************************************/
/****   Source code for Parakeet ProE Firmware Update                 ****/
/****   Program Name: upgrade.c                                       ****/
/****   Version 1.0                                                   ****/
/****   November 30, 2022                                             ****/
/*************************************************************************/

/*************************************************************************/
/****   Command Line Interface Parameters                             ****/
/****    Example:                                                     ****/
/****       upgrade -d 192.168.0.5 -p 6543 -f Parakeet.lhl            ****/
/****   -d	Lidar IP Address     ie. 192.168.0.5                      ****/
/****   -p 	Destination IP Port     ie. 6543                          ****/
/****   -f	Firmware File Name   ie. Parakeet.lhl                     ****/
/*************************************************************************/


//****  Include required Library headers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <time.h>
#include <netdb.h>


//**** Define Constants for use in software

#define OP_FLASH_ERASE      0xFE00EEEE   // Op Code to Initiate Sensor Flash Erase
#define OP_WRITE_IAP        0xFE00AAAA   // Op Code In Application Programming Write code portion to Flash
#define OP_FRIMWARE_RESET   0xFE00BBBB   // Op Code to Restart Firmware

#define PACK_PREAMBLE       0X484C		// Preamble code for messages to the sensor
#define F_PACK              0x0046

#define UDP_HEADER          8        // Length of UDP Header in bytes (4 Words)



//**** Data Structures

// Structure for building UDP data blocks to send to the sensor for programming the FLASH Program Memory
struct FirmwareFile {
    uint32_t code;
    uint32_t len;
    uint32_t sent;
    uint32_t crc;
    uint8_t date[4];
    uint8_t unused[120];
    int8_t describe[512];
    uint8_t buffer[0];
};

// Structure for building UDP commands to send to the sensor
struct FirmwarePart {
    uint32_t offset;
    uint32_t crc;
    uint32_t data[128];
};

// Structure for capturing UDP response packet data from the sensor
struct FirmWriteResp {
    uint32_t offset;
    int32_t result;
    int8_t msg[128];
};

// Structure for the UDP packet header 
struct UdpHeader {
    uint16_t sign;
    uint16_t cmd;
    uint16_t sn;
    uint16_t len;
};


//**** Software Routines

// crc32 Calculation
unsigned int stm32crc(const unsigned int *ptr, unsigned int len)
{
    unsigned int xbit, data;
    unsigned int crc32 = 0xFFFFFFFF;
    const unsigned int polynomial = 0x04c11db7;

    for (unsigned int i = 0; i < len; i++)
    {
        xbit = 1 << 31;
        data = ptr[i];
        for (unsigned int bits = 0; bits < 32; bits++)
        {
            if (crc32 & 0x80000000)
            {
                crc32 <<= 1;
                crc32 ^= polynomial;
            }
            else
                crc32 <<= 1;

            if (data & xbit)
                crc32 ^= polynomial;

            xbit >>= 1;
        }
    }
    return crc32;
}

//  Build UDP Packet and calculate CRC32
int packUdp(int len, void* payload, void* buf)
{
    struct UdpHeader* hdr = (struct UdpHeader*)buf;
    hdr->sign = PACK_PREAMBLE;
    hdr->cmd = F_PACK;
    hdr->sn = rand();
    hdr->len = len;

    int len4 = ((len+3)>>2)*4;   // round to nearest multiple of 4
    memcpy(buf + UDP_HEADER, payload, len);

    unsigned int* pcrc = (unsigned int*)((uint8_t*)buf + UDP_HEADER + len4);
    *pcrc = stm32crc((unsigned int*)buf, len4 / 4 + 2);

    return len4 + 12;
}


//  Unpack UDP packet and verify CRC32  
int unpackUdp(int len, uint8_t* buf, struct UdpHeader* hdr, uint8_t** payload)
{
    memcpy(hdr, buf, UDP_HEADER);

	  // Includes correct PreAmble?
    if (hdr->sign != PACK_PREAMBLE)
    {
		return -1;
    }
	  // is the right length?
    if (hdr->len + 12 != len)
    {
        return -2;
    }

    unsigned int* crc = (unsigned int*)(buf + UDP_HEADER + hdr->len);

	  // is the CRC32 Correct?
    if (*crc != stm32crc((unsigned int*)buf, hdr->len / 4 + 2))
    {
        return -3;
    }

    *payload = buf + UDP_HEADER;

    return hdr->len;
}


// Routine to send UDP message and Recieve Response from sensor
int udpTalk(int fdUdp, const char* devIp, int devPort,
            int nSend, void* sendBuf,
            int* nRecv, void* recvBuf)
{
    struct sockaddr_in sin1;
    socklen_t sin_size = sizeof(sin1);
    bzero( &sin1, sizeof(struct sockaddr_in) );
    sin1.sin_family = AF_INET;
    sin1.sin_addr.s_addr = inet_addr(devIp);
    sin1.sin_port = htons(devPort);

    uint8_t udpBuf[1024];
    int len = packUdp(nSend, sendBuf, udpBuf);
    struct UdpHeader* cmd = (struct UdpHeader*)udpBuf;

        // Send UDP Packet to Sensor
    sendto(fdUdp, (char*)udpBuf, len, 0, (struct sockaddr *)&sin1, sin_size);

	  // Wait 3 seconds for the response from the sensor
    int i;
    for (i=0; i<3; i++)
    {
        struct timeval to;
        to.tv_sec = 1;
        to.tv_usec = 0;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fdUdp, &readfds);

        int rt = select(fdUdp+1, &readfds, NULL, NULL, &to);

        if (rt <= 0) continue;

        if (FD_ISSET(fdUdp, &readfds) )
        {
            struct sockaddr_in remote;
            socklen_t sin_size = sizeof(remote);

            uint8_t respBuf[1024];
            int len = recvfrom(fdUdp, (char*)respBuf, sizeof(respBuf), 0,
                (struct sockaddr *)&remote, &sin_size);

            struct UdpHeader resp;
            uint8_t* payload;
            rt = unpackUdp(len, respBuf, &resp, &payload);
            // printf("unpack len %d : %d payload %d\n", len, rt, resp.len); // Debug printf 

            if (rt > 0 && resp.sn == cmd->sn)
            {
				// Good response packet received move it into buffer for calling routine
                *nRecv = resp.len;
                memcpy(recvBuf, payload, resp.len);
                return 0;
            }
            printf("unknown pack %x len %d\n", resp.cmd, resp.len);
        }
    }
	  // No valid response packet received
    return -1;
}


// Routine to send command and data packets to the sensor over UDP.  Capture response UDP packet from sensor.
int upgradeTalk(int fdUdp, const char* devIp, int devPort,
    struct FirmwarePart* part,
    struct FirmWriteResp* resp)
{
    uint8_t respBuf[1024];
    int nResp;

    // send message to transmission routine
    int rt = udpTalk(fdUdp, devIp, devPort, sizeof(struct FirmwarePart), part, &nResp, respBuf);
    if (rt != 0 || nResp < sizeof(struct FirmWriteResp))
    {
        printf("udp talk error %d %d\n", rt, nResp);
        return -1;
    }
    memcpy(resp, respBuf, sizeof(struct FirmWriteResp));

    // Examine response data offset comparied to sent offset.  Match?
    if (resp->offset != part->offset)
    {
        printf("offset %x != %x\n", resp->offset, part->offset);
        return -2;
    }

    return 0;
}


// Read through file data structure and send all the 512 byte data blocks to the sensor

int Upgrade(const char* devIp, int devPort, int binLen, char* binBuf)
{
    int fdUdp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // Build Firmware Message Structure to give sensor Instructions  Start-begin Escalate-upgrade
    struct FirmwarePart start;       // Create Stucture for UDP Command Message
    start.offset = OP_FLASH_ERASE;   // Set ERASE FLASH Command
    start.crc = 0xffffffff;          // Set CRC32 to initial calculation value
    start.data[0] = binLen;          // Set beginning of data field to length of upgrade firmware

    struct FirmWriteResp resp;		// Create structure to receive response message from sensor
    printf("send erase length %d\n", binLen);
    int rt = upgradeTalk(fdUdp, devIp, devPort, &start, &resp);
    if (rt != 0) {
		  // Erase Flash Fail
        printf("send start, resp %d\n", rt);
        return -1;
    }
      // Erase Flash Successful
    printf("start return %d, resp %d : %s\n", rt, resp.result, resp.msg);

    // Iterate through upgrade firmware file to send all packets
    uint32_t binOffset = 0;
    while (binOffset < binLen)
    {
        // Get next block of 512 bytes of firmware
        struct FirmwarePart part;
        part.offset = binOffset;
        memcpy(part.data, binBuf+binOffset, 512); // Get next block to deliver 512 Bytes
        part.crc = stm32crc(part.data, 128);

        while (1)
        {
            // printf("sending part offset %x\n", binOffset);  // Debug printf
            rt = upgradeTalk(fdUdp, devIp, devPort, &part, &resp);
            if (rt != 0) {
                printf("send part offset %x return %d\n", binOffset, rt);
                continue;
            }
            printf("offset %x return %d : %s\n", binOffset, resp.result, resp.msg);
            if (resp.result == 0) break;
        }
        binOffset += 512;
    }
      //  Sending upgrade code complete
      //  Assembble Command to send IAP at Data Transport Complete In Application Programming
    struct FirmwarePart iap;
    iap.offset = OP_WRITE_IAP;// Command Transport Complete In application Programming
    iap.crc = 0xffffffff;
    iap.data[0] = binLen; // File Data Length

    printf("sending iap length %d\n", binLen);
    rt = upgradeTalk(fdUdp, devIp, devPort, &iap, &resp);
    if (rt != 0) {
        printf("send iap error %d\n", rt);
        return -1;
    }
    printf("iap return %d %s\n", resp.result, resp.msg);

      // Assemble command to reset the sensor.  Response will fail since sensor resets immediately
    struct FirmwarePart reset;
    reset.offset = OP_FRIMWARE_RESET; // Reset Firmware Confirm
    reset.crc = 0xffffffff;
    reset.data[0] = 0xabcd1234; // Signature for Reset command Double Verify

    printf("sending reset\n");
    upgradeTalk(fdUdp, devIp, devPort, &reset, &resp);

    return 0;
}



// Open - Read Upgrade File - Close  Verify Header information
int loadFirmware(const char* path, struct FirmwareFile** ppFF)
{
    struct stat st;
    if ( stat(path, &st) != 0)
    {
		// file not found
		printf("can not read %s info\n", path);
        return -1;
    }

    // Open firmware file
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
		// file cannot be opened
        printf("can not open file %s\n", path);
        return -2;
    }

    // create structure in allocated memory to hold the firmware file
    struct FirmwareFile* binFile = (struct FirmwareFile*)malloc(st.st_size);

    // read the entire file into the binFile structure
    fread(binFile, 1, st.st_size, fp);
    fclose(fp);  // close the firmware file

    if (binFile->code != 0xb18e03ea )
    {
		// firmware file header signature code does not match
        printf("file header code %x\n", binFile->code);
        return -3;
    }
    if ((binFile->len % 512) != 0 )
    {
		// firmware file data is not a mutliple of 512 bytes in length
        printf("len %d\n", binFile->len);
        return -4;
    }
    if (binFile->len + sizeof(struct FirmwareFile) != st.st_size)
    {
		// firmware file total file length is not correct
        printf("len error %ld != %ld\n", binFile->len + sizeof(struct FirmwareFile), st.st_size);
        return -5;
    }
    if (binFile->crc != stm32crc((uint32_t*)(binFile->buffer), binFile->len/4))
    {
        // firmware file header crc32 is not correct
        printf("crc error\n");
        return -6;
    }
    *ppFF = binFile;
    return 0;
}

//**** main software routine
int main(int argc, char* argv[])
{
    int devPort = -1;
    char devIp[128] = "";
    char binPath[125] = "";

    // Read in command line arguments
    int ch;
    while((ch = getopt(argc, argv, "d:p:f:")) != -1)
    {
        switch(ch) {
            case 'd': strcpy(devIp, optarg); break;
            case 'p': devPort = atoi(optarg); break;
            case 'f': strcpy(binPath, optarg); break;
            default: break;
        }
    }

    // IP Port and Device IP Address provided?  Gross check for existance.
    if (devPort < 0 || strlen(devIp) == 0)
    {
        printf("%s -d lidar_ip_address -p 6543 -f firmware.bin\n", argv[0]);
        return -1;
    }

    // Open Firmware file and Verfiy Header data. Is it a valid Firmware File?
    struct FirmwareFile* binFile;
    if ( loadFirmware(binPath, &binFile) != 0)
    {
        printf("load firmware %s fail\n", binPath);
        return -2;
    }
    printf("firmware date 20%02d/%02d/%02d - %02d:00   %s\n",
        binFile->date[0], binFile->date[1], binFile->date[2], binFile->date[3],
        binFile->describe);

    // Firmware file is valid, Send upgrade firmware to the sensor.
    return Upgrade(devIp, devPort, binFile->len, binFile->buffer);
}

