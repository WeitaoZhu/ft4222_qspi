#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "ftd2xx.h"
#include "libft4222.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>

// SPI Master can assert SS0O in single mode
// SS0O and SS1O in dual mode, and
// SS0O, SS1O, SS2O and SS3O in quad mode.
#define SLAVE_SELECT(x)      (1 << (x))

#define QSPI_SYS_CLK         80000000
#define QSPI_ACCESS_WINDOW   0x02000000
#define QSPI_SET_BASE_ADDR   0x02000004
#define QSPI_DUMP_CMD_MAX    128
#define QSPI_DUMP_MAX_SIZE   4096
#define QSPI_DUMP_COL_NUM    4
#define QSPI_DUMP_WORD       4
#define QSPI_MULTI_WR_DELAY  10

#define QSPI_WR_OP_MASK           (1<<7)
#define QSPI_WRITE_OP             (1<<7)
#define QSPI_READ_OP              (0<<7)

#define QSPI_TRANS_TYPE_MASK      (3<<5)
#define QSPI_TRANS_DATA           (0<<5)
#define QSPI_READ_REQUEST         (1<<5)
#define QSPI_TRANS_STATUS         (2<<5)
#define QSPI_READ_DUMMY           (3<<5)

#define QSPI_WAIT_CYCLE_MASK      (3<<3)

#define QSPI_DATA_LENGTH_MASK     0x07

static int debug_printf=0;
static const char *const short_options = "bhvrwga:D:d:p:";
static const struct option long_options[] = {
   {"base", no_argument, NULL, 'b'},
   {"help", no_argument, NULL, 'h'},
   {"version", no_argument, NULL, 'v'},
   {"addr", required_argument, NULL, 'a'},
   {"Data", required_argument, NULL, 'D'},
   {"div", required_argument, NULL, 'd'},
   {"dump", required_argument, NULL, 'p'},
   {"debug", no_argument, NULL, 'g'},
   {"write", no_argument, NULL, 'w'},
   {"read", no_argument, NULL, 'r'},
   {NULL, no_argument, NULL, 0},
};

static void print_usage(FILE * stream, char *app_name, int exit_code)
{
   fprintf(stream, "Usage: %s [options]\n", app_name);
   fprintf(stream,
      " -a  --Addr <address>      Setting QSPI access address.\n"
      " -b  --base                Display SPI2AHB Base Address.\n"
      " -d  --div <division>      Setting QSPI CLOCK with 80MHz/<division>.\n"
      " -D  --Data <value>        Setting QSPI Send data value.\n"
      " -g  --debug               Display QSPI W/R Send Data Info.\n"
      " -h  --help                Display this usage information.\n"
      " -p  --dump <size>         Dump Address size Context.\n"
	  " -r  --read                Setting QSPI Read Operation.\n"
      " -w  --write               Setting QSPI Write Operation.\n"
      " -v  --version             Display FT4222 Chip version and LibFT4222 version.\n");
 
   exit(exit_code);
}

static int get_int_number(const char *_str)
{
	char *end;
	long int num;

	num = strtol(_str, &end, 10);
	if (*end != '\0')
		return -1;

	return num;
}

static unsigned int get_ul_number(const char *_str)
{
	char *end;
	unsigned long addr;

	addr = strtoul(_str, &end, 16);
	if (*end != '\0')
		return -1;

	return addr;
}

static uint32_t swapLong(uint32_t ldata)
{
   uint32_t temp;

   temp =   ((ldata & 0xFF000000U) >> 24)
          | ((ldata & 0x00FF0000U) >> 8 )
          | ((ldata & 0x0000FF00U) << 8 )
          | ((ldata & 0x000000FFU) << 24);
   return(temp);
}


static void msleep(unsigned int msecs)
{
	usleep(msecs*1000);
}
static void showVersion(FT_HANDLE ftHandle)
{
    FT_STATUS            ftStatus;
    FT4222_STATUS        ft4222Status;
    FT4222_Version       ft4222Version;

    // Get version of library and chip.    
    ft4222Status = FT4222_GetVersion(ftHandle,
                                     &ft4222Version);
    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_GetVersion failed (error %d)\n",
               (int)ft4222Status);
    }
    else
    {
        printf("Chip version: %08X, LibFT4222 version: %08X\n",
               (unsigned int)ft4222Version.chipVersion,
               (unsigned int)ft4222Version.dllVersion);
    }
	return;
}

static FT4222_SPIClock ft4222_convert_qspiclk(int division)
{
	FT4222_SPIClock           ftQspiClk = CLK_DIV_128;
	switch (division) {
		case 0:
		case 1:
				ftQspiClk = CLK_NONE;
			break;
		case 2:
				ftQspiClk = CLK_DIV_2;
			break;
		case 4:
				ftQspiClk = CLK_DIV_4;
			break;
		case 8:
				ftQspiClk = CLK_DIV_8;
			break;
		case 16:
				ftQspiClk = CLK_DIV_16;
			break;
		case 32:
				ftQspiClk = CLK_DIV_32;
			break;
		case 64:
				ftQspiClk = CLK_DIV_64;
			break;
		case 128:
				ftQspiClk = CLK_DIV_128;
			break;
		case 256:
				ftQspiClk = CLK_DIV_256;
			break;
		case 512:
				ftQspiClk = CLK_DIV_512;
			break;
		default:
			if (division == 3) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_2");
				ftQspiClk = CLK_DIV_2;
			}

			if (division > 4 && division < 8) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_4");
				ftQspiClk = CLK_DIV_4;
			}

			if (division > 8 && division < 16) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_8");
				ftQspiClk = CLK_DIV_8;
			}

			if (division > 16 && division < 32) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_16");
				ftQspiClk = CLK_DIV_16;
			}

			if (division > 32 && division < 64) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_32");
				ftQspiClk = CLK_DIV_32;
			}

			if (division > 64 && division < 128) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_64");
				ftQspiClk = CLK_DIV_64;
			}

			if (division > 128 && division < 256) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_128");
				ftQspiClk = CLK_DIV_128;
			}

			if (division > 256 && division < 512) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_256");
				ftQspiClk = CLK_DIV_256;
			}

			if (division > 512) {
				printf("  FT4222 SPIClock not support div %d, setting %s as default!\n",division,"CLK_DIV_512");
				ftQspiClk = CLK_DIV_512;
			}
			break;
	}
	return ftQspiClk;
}

static int ft4222_qspi_write_nword(FT_HANDLE ftHandle, unsigned int offset, uint8_t *buffer, uint16_t bytes)
{
    int success = 1, i =0;
	uint8_t *writeBuffer =  NULL;
	uint8_t cmd[4]= {0};
	uint8_t data_length;
	FT4222_STATUS  ft4222Status;
	uint32_t sizeOfRead;

	switch(bytes)
	{
		case 4:
			data_length = 0;
			break;
		case 16:
			data_length = 1;
			break;
		case 32:
			data_length = 2;
			break;
		case 64:
			data_length = 3;
			break;
		case 128:
			data_length = 4;
			break;
		case 256:
			data_length = 5;
			break;
		default:
			break;
	}
	cmd[0] = QSPI_WRITE_OP | QSPI_TRANS_DATA | data_length;
	cmd[1] = (offset >> 18) & 0xFF;
	cmd[2] = (offset >> 10) & 0xFF;
	cmd[3] = (offset >> 2) & 0xFF;

	writeBuffer = malloc(sizeof(cmd) + bytes);

	memset(writeBuffer,0x0,sizeof(cmd) + bytes);
	memcpy(writeBuffer, cmd, sizeof(cmd));
	memcpy(writeBuffer + sizeof(cmd), buffer, bytes);

	if (debug_printf) {
		printf("[QSPI Write OP]\n");
		printf("writeBuffer:");
		for(i=0;i < (sizeof(cmd) + bytes); i++ )
			printf("%02x ", *(writeBuffer + i));
		printf("\n");
		printf("\n");
	}

	ft4222Status = FT4222_SPIMaster_MultiReadWrite(
						ftHandle,
						NULL, //readBuffer
						writeBuffer,
						0, //singleWriteBytes = 0
						bytes + sizeof(cmd), //multiWriteBytes
						0, //multiReadBytes = 0
						&sizeOfRead);

	msleep(QSPI_MULTI_WR_DELAY);

    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPIMaster_MultiReadWrite failed (error %d)!\n",
               ft4222Status);
        success = 0;
        goto exit;
    }
exit:
	free(writeBuffer);
    return success;
}

static int ft4222_qspi_read_nword(FT_HANDLE ftHandle, unsigned int offset, uint8_t *buffer, uint16_t bytes)
{
    int success = 1 ,cnt = 0;
	uint8_t cmd[4]= {0};
	uint8_t data_length;
	FT4222_STATUS  ft4222Status;
	uint32_t sizeOfRead;

	switch(bytes)
	{
		case 4:
			data_length = 0;
			break;
		case 16:
			data_length = 1;
			break;
		case 32:
			data_length = 2;
			break;
		case 64:
			data_length = 3;
			break;
		case 128:
			data_length = 4;
			break;
		case 256:
			data_length = 5;
			break;
		default:
			break;
	}

	//Send Read Request
	cmd[0] = QSPI_READ_OP | QSPI_READ_REQUEST | data_length;
	cmd[1] = (offset >> 18) & 0xFF;
	cmd[2] = (offset >> 10) & 0xFF;
	cmd[3] = (offset >> 2) & 0xFF;

	if (debug_printf) {
		printf("[QSPI Read OP]\n");
		printf("Read Request cmd:%02x %02x %02x %02x\n",cmd[0],cmd[1],cmd[2],cmd[3]);
	}

	ft4222Status = FT4222_SPIMaster_MultiReadWrite(
						ftHandle,
						NULL, //readBuffer
						cmd, //writeBuffer
						0, //singleWriteBytes = 0
						sizeof(cmd), //multiWriteBytes
						0, //multiReadBytes = 0
						&sizeOfRead);
	msleep(QSPI_MULTI_WR_DELAY);

    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPIMaster_MultiReadWrite failed (error %d)!\n",
               ft4222Status);
        success = 0;
        goto exit;
    }

    //Send Read Data
	cmd[0] = QSPI_READ_OP | QSPI_TRANS_DATA | data_length;
	ft4222Status = FT4222_SPIMaster_MultiReadWrite(
						ftHandle,
						buffer, //readBuffer
						cmd, //writeBuffer
						0, //singleWriteBytes = 0
						1, //multiWriteBytes
						bytes, //multiReadBytes = 0
						&sizeOfRead);

	msleep(QSPI_MULTI_WR_DELAY);

    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPIMaster_MultiReadWrite failed (error %d)!\n",
               ft4222Status);
        success = 0;
        goto exit;
    }

	if (debug_printf) {
		printf("Read Data cmd:%02x\n",cmd[0]);
		printf("Read Data:");
		for(cnt=0; cnt < sizeOfRead; cnt++)
			printf("%02x ", *(buffer + cnt));
		printf("\n");
		printf("\n");
	}

exit:
    return success;
}

static int ft4222_qspi_get_base(FT_HANDLE ftHandle, uint32_t *paddr)
{
    int success = 1;
	uint8_t base_addr[4]= {0};

    if (!ft4222_qspi_read_nword(ftHandle, QSPI_SET_BASE_ADDR, base_addr, sizeof(base_addr)))
    {
        printf("Failed to ft4222_qspi_read_nword.\n");
		success = 0;
        goto exit;
    }

	*paddr = (base_addr[0] << 24) | (base_addr[1] << 16) | (base_addr[2] << 8) | base_addr[3];
	//printf("SPI2AHB Base Addr 0x%08x\n", *paddr);
exit:
    return success;
}

static int ft4222_qspi_check_base(FT_HANDLE ftHandle, uint32_t mem_addr)
{
	int success = 1, retry=0;
	uint8_t  qspi_base[4]= {0};
	uint32_t qspi_base_addr =0;
	uint32_t base_addr  =(mem_addr/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;

	if (!ft4222_qspi_get_base(ftHandle, &qspi_base_addr))
	{
        printf("Failed to ft4222_qspi_get_base.\n");
		success = 0;
        goto exit;
	}

retry_switch:

	// Check QSPI Base Address
	if (qspi_base_addr != base_addr)
	{
		qspi_base[0] = (base_addr >> 24) & 0xFF;
		qspi_base[1] = (base_addr >> 16) & 0xFF;
		qspi_base[2] = (base_addr >>  8) & 0xFF;
		qspi_base[3] = (base_addr >>  0) & 0xFF;

		if (!ft4222_qspi_write_nword(ftHandle, QSPI_SET_BASE_ADDR, qspi_base, sizeof(qspi_base)))
		{
			printf("Failed switch base address to 0x%8x.\n",base_addr);
			success = 0;
			goto exit;
		}
		msleep(500);

		if (!ft4222_qspi_get_base(ftHandle, &qspi_base_addr))
		{
			printf("Failed to ft4222_qspi_get_base.\n");
			success = 0;
			goto exit;
		}

		if ((qspi_base_addr != base_addr) && (retry < 3)) {
			retry ++;
			goto retry_switch;
		}

		if (retry >= 3) {
			printf("Failed to retry switch new base 0x%08x.\n",base_addr);
			success = 0;
			goto exit;
		}
	}

exit:
    return success;
}

static int ft4222_qspi_memory_write(FT_HANDLE ftHandle, uint32_t mem_addr, uint8_t *buffer, uint16_t bytes)
{
	int success = 1;
	uint32_t offset_addr=(mem_addr%QSPI_ACCESS_WINDOW);

	if (!ft4222_qspi_check_base(ftHandle, mem_addr))
	{
        printf("Failed to check and rebuild base address.\n");
		success = 0;
        goto exit;
	}

	// Send QSPI Data
	if (!ft4222_qspi_write_nword(ftHandle, offset_addr, buffer, bytes))
	{
		printf("Failed ft4222_qspi_write_nword send data.\n");
		success = 0;
		goto exit;
	}

exit:
    return success;
}

static int ft4222_qspi_memory_read(FT_HANDLE ftHandle, uint32_t mem_addr, uint8_t *buffer, uint16_t bytes)
{
	int success = 1;
	uint32_t offset_addr=(mem_addr%QSPI_ACCESS_WINDOW);

	if (!ft4222_qspi_check_base(ftHandle, mem_addr))
	{
        printf("Failed to check and rebuild base address.\n");
		success = 0;
        goto exit;
	}

	// Send QSPI Data
	if (!ft4222_qspi_read_nword(ftHandle, offset_addr, buffer, bytes))
	{
		printf("Failed ft4222_qspi_read_nword send data.\n");
		success = 0;
		goto exit;
	}

exit:
    return success;
}

static int ft4222_qspi_memory_read_word(FT_HANDLE ftHandle, uint32_t mem_addr, uint32_t *pdata)
{
    int success = 1;
	uint8_t  qspi_data[4]= {0};
	if (!ft4222_qspi_memory_read(ftHandle, mem_addr, qspi_data, 4))
	{
        printf("Failed to ft4222_qspi_memory_read 4 bytes.\n");
		success = 0;
        goto exit;
	}
	*pdata = (qspi_data[0] << 24) | (qspi_data[1] << 16) | (qspi_data[2] << 8) | qspi_data[3];
exit:
    return success;
}


static int ft4222_qspi_cmd_dump(FT_HANDLE ftHandle, uint32_t mem_addr, uint16_t size)
{
    int success = 1, row =0, col =0;
	uint8_t *buffer = NULL;
	uint32_t start_base  =(mem_addr/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;
	uint32_t end_base  =((mem_addr + size)/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;
	uint32_t offset_addr=(mem_addr%QSPI_ACCESS_WINDOW);

	if (size > QSPI_DUMP_CMD_MAX) {
        printf("QSPI CMD Dump size %d exceed max %d.\n",(int)size ,QSPI_DUMP_CMD_MAX);
		success = 0;
        goto exit;
	}

	if ((start_base != end_base) && ((mem_addr + size)%QSPI_ACCESS_WINDOW)){
        printf("QSPI CMD Dump from 0x%08x to 0x%08x, exceed next 32M windows 0x%08x.\n",mem_addr, (mem_addr + size), end_base);
		success = 0;
        goto exit;
	}

	buffer = malloc(size);

	if (!ft4222_qspi_memory_read(ftHandle, mem_addr, buffer, size))
	{
        printf("Failed to ft4222_qspi_memory_read 4 bytes.\n");
		success = 0;
        goto exit;
	}

	for(row=0; row < size/(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD); row++)
	{
		printf("%08x : ", mem_addr + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM));
		for(col=0; col < QSPI_DUMP_COL_NUM; col++)
		{
			printf("%08x ", swapLong(*((uint32_t *)(buffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD))));
		}
		printf("\n");
	}
exit:
    return success;
}


static int ft4222_qspi_memory_dump(FT_HANDLE ftHandle, uint32_t mem_addr, uint16_t size)
{
    int success = 1, cmd_time;
	uint32_t qspi_addr = 0;

	if (size > QSPI_DUMP_MAX_SIZE) {
        printf("QSPI Dump memory size %d exceed max %d.\n",(int)size ,QSPI_DUMP_MAX_SIZE);
		success = 0;
        goto exit;
	}

	cmd_time = (size/QSPI_DUMP_CMD_MAX);

	for (cmd_time = 0; cmd_time < (size/QSPI_DUMP_CMD_MAX) ; cmd_time++)
	{
		qspi_addr = mem_addr + (QSPI_DUMP_CMD_MAX * cmd_time);

		if (!ft4222_qspi_cmd_dump(ftHandle, qspi_addr, QSPI_DUMP_CMD_MAX))
		{
			printf("Failed to ft4222_qspi_cmd_dump.\n");
			success = 0;
			goto exit;
		}
		msleep(10);
	}

	if (size%QSPI_DUMP_CMD_MAX)
	{
		qspi_addr = mem_addr + (QSPI_DUMP_CMD_MAX * (cmd_time + 1));

		if (!ft4222_qspi_cmd_dump(ftHandle, qspi_addr, size%QSPI_DUMP_CMD_MAX))
		{
			printf("Failed to ft4222_qspi_cmd_dump.\n");
			success = 0;
			goto exit;
		}
	}

exit:
    return success;
}


static int ft4222_qspi_memory_write_word(FT_HANDLE ftHandle, uint32_t mem_addr, uint32_t mem_data)
{
	uint8_t  qspi_data[4]= {0};
	qspi_data[0] = (mem_data >> 24) & 0xFF;
	qspi_data[1] = (mem_data >> 16) & 0xFF;
	qspi_data[2] = (mem_data >>  8) & 0xFF;
	qspi_data[3] = (mem_data >>  0) & 0xFF;
	return ft4222_qspi_memory_write(ftHandle, mem_addr, qspi_data, 4);
}

int main(int argc, char **argv)
{
   FT_STATUS                 ftStatus;
   FT4222_STATUS             ft4222Status;
   FT_HANDLE                 ftHandle = (FT_HANDLE)NULL;
   FT_DEVICE_LIST_INFO_NODE  *devInfo = NULL;
   FT4222_SPIClock           ftQspiClk = 0;
   DWORD                     numDevs = 0;
   DWORD                     ft4222_LocId;
   unsigned int              addr,spi2ahb_base,data_value,tmp_value;
   int                       i;
   int                       retCode = 0;
   int                       found4222 = 0;	

   int fd;
   int division,write_op,read_op,addr_set,data_set,show_base,show_ft4222_ver,dump_show,dump_size;
   int next_option;   /* getopt iteration var */
 
   /* Init variables */
   division = 128;
   ftQspiClk = ft4222_convert_qspiclk(division);  //Set QSPI CLK default CLK_DIV_128 80M/128=625Khz
   write_op = 0;
   read_op = 0;
   addr_set = 0;
   data_set = 0;
   tmp_value = 0;
   show_base =0;
   show_ft4222_ver =0;
   dump_show = 0;
   
    ftStatus = FT_CreateDeviceInfoList(&numDevs);
    if (ftStatus != FT_OK) 
    {
        printf("FT_CreateDeviceInfoList failed (error code %d)\n", 
               (int)ftStatus);
        retCode = ftStatus;
        goto exit;
    }
    
    if (numDevs == 0)
    {
        printf("No devices connected.\n");
        retCode = -20;
        goto exit;
    }

    /* Allocate storage */
    devInfo = calloc((size_t)numDevs,
                     sizeof(FT_DEVICE_LIST_INFO_NODE));
    if (devInfo == NULL)
    {
        printf("Allocation failure.\n");
        retCode = -30;
        goto exit;
    }
    
    /* Populate the list of info nodes */
    ftStatus = FT_GetDeviceInfoList(devInfo, &numDevs);
    if (ftStatus != FT_OK)
    {
        printf("FT_GetDeviceInfoList failed (error code %d)\n",
               (int)ftStatus);
        retCode = ftStatus;
        goto exit;
    }

    for (i = 0; i < (int)numDevs; i++) 
    {
        if (devInfo[i].Type == FT_DEVICE_4222H_0  ||
            devInfo[i].Type == FT_DEVICE_4222H_1_2)
        {
            // In mode 0, the FT4222H presents two interfaces: A and B.
            // In modes 1 and 2, it presents four interfaces: A, B, C and D.

            size_t descLen = strlen(devInfo[i].Description);
            
            if ('A' == devInfo[i].Description[descLen - 1])
            {
				// Interface A may be configured as an SPI master.
				ft4222_LocId = devInfo[i].LocId;
				break;
            }
            else
            {
                // Interface B, C or D.
                // No need to repeat version info of same chip.
            }
            found4222++;
        }
    }

   /* Parse options if any */
   do {
      next_option = getopt_long(argc, argv, short_options,
                 long_options, NULL);
      switch (next_option) {
      case 'b':
			show_base = 1;
		 break;
      case 'a':
	     addr = get_ul_number(optarg);
		 addr_set = 1;
         break;
      case 'D':
	     data_value = get_ul_number(optarg);
		 data_set =1;
         break;
      case 'd':
	     division = atoi(optarg);
		 ftQspiClk = ft4222_convert_qspiclk(division);
         break;
      case 'g':
			debug_printf = 1;
         break;
      case 'h':
         print_usage(stdout, argv[0], EXIT_SUCCESS);
      case 'p':
			dump_size = atoi(optarg);
			dump_show = 1;
         break;
      case 'r':
			read_op = 1;
         break;
      case 'v':
			show_ft4222_ver = 1;
		break;
      case 'w':
			write_op = 1;
         break;
      case '?':   /* Invalid options */
         print_usage(stderr, argv[0], EXIT_FAILURE);
      case -1:   /* Done with options */
         break;
      default:   /* Unexpected stuffs */
         abort();
      }
   } while (next_option != -1);

    ftStatus = FT_OpenEx((PVOID)(uintptr_t)ft4222_LocId,
                         FT_OPEN_BY_LOCATION,
                         &ftHandle);
    if (ftStatus != FT_OK)
    {
        printf("FT_OpenEx failed (error %d)\n",
               (int)ftStatus);
		retCode = ftStatus;
        goto exit;
    }

	if (show_ft4222_ver)
		showVersion(ftHandle);

	if (debug_printf)
		printf("[QSPI CLK] %d Hz\n",QSPI_SYS_CLK/division);

    if (write_op)
    {
	    if ((addr_set == 0) || (data_set == 0))
	    {
			printf("ft4222 work in write mode,%s %s\n",(addr_set ? "":"addr is missing"),(data_set ? "":"data is missing"));
			retCode = -20;
			goto ft4222_exit;
	    }
    }

    if (read_op)
    {
	    if (addr_set == 0)
	    {
			printf("ft4222 work in write mode,addr is missing\n");
			retCode = -20;
			goto ft4222_exit;
	    }
    }

    if (dump_show)
    {
	    if (addr_set == 0)
	    {
			printf("ft4222 work in dump mode,addr is missing\n");
			retCode = -30;
			goto ft4222_exit;
	    }
    }
    // Configure the FT4222 as an SPI Master.
    ft4222Status = FT4222_SPIMaster_Init(
                        ftHandle,
                        SPI_IO_QUAD, // 4 channel
                        ftQspiClk, // 80 MHz / 128 == 625KHz
                        CLK_IDLE_LOW, // clock idles at logic 0
                        CLK_LEADING, // data captured on rising edge
                        SLAVE_SELECT(0)); // Use SS0O for slave-select
    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPIMaster_Init failed (error %d)\n",
               (int)ft4222Status);
        goto ft4222_exit;
    }

    ft4222Status = FT4222_SPI_SetDrivingStrength(ftHandle,
                                                 DS_12MA,
                                                 DS_12MA,
                                                 DS_12MA);
    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPI_SetDrivingStrength failed (error %d)\n",
               (int)ft4222Status);
        goto ft4222_exit;
    }

	if (show_base) {
		ft4222_qspi_get_base(ftHandle, &tmp_value);
		printf("QSPI2AHB Current Base Address 0x%08x\n", tmp_value);
	}
	if (write_op) {
		ft4222_qspi_memory_write_word(ftHandle, addr, data_value);
	}

	if (read_op) {
		ft4222_qspi_memory_read_word(ftHandle, addr, &tmp_value);
		printf("%08x : %08x\n", addr, tmp_value);
	}

	if (dump_show) {
		ft4222_qspi_memory_dump(ftHandle, addr, dump_size);
	}

ft4222_exit:
    (void)FT_Close(ftHandle);
exit:
    free(devInfo);
    return retCode;
}
