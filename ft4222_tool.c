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
#include "version.h"

// SPI Master can assert SS0O in single mode
// SS0O and SS1O in dual mode, and
// SS0O, SS1O, SS2O and SS3O in quad mode.
#define SLAVE_SELECT(x)      (1 << (x))

#define QSPI_SYS_CLK         80000000
#define QSPI_ACCESS_WINDOW   0x02000000
#define QSPI_SET_BASE_ADDR   0x02000004
#define QSPI_DEFAULT_DIV     512
#define QSPI_SYS_CLK         80000000
#define QSPI_CMD_DATA_MAX    128
#define QSPI_CMD_WRITE_MAX   128
#define QSPI_CMD_READ_MAX    128
#define QSPI_DUMP_MAX_SIZE   4096
#define QSPI_SCRIPT_MAX_SIZE 4096
#define QSPI_DUMP_COL_NUM    4
#define QSPI_DUMP_WORD       4
#define QSPI_MULTI_WR_DELAY  50
#define QSPI_MULTI_WR_RETRY  10
#define QSPI_SWAP_WORD       1
#define QSPI_NO_SWAP_WORD    0
#define QSPI_STATUS_ENABLE   1


#define QSPI_WR_READY             (1<<7)
#define QSPI_WR_OP_MASK           (1<<7)
#define QSPI_WRITE_OP             (1<<7)
#define QSPI_READ_OP              (0<<7)

#define QSPI_TRANS_TYPE_MASK      (3<<5)
#define QSPI_TRANS_DATA           (0<<5)
#define QSPI_READ_REQUEST         (1<<5)
#define QSPI_TRANS_STATUS         (2<<5)
#define QSPI_READ_DUMMY           (3<<5)

#define QSPI_WAIT_CYCLE_MASK      (3<<3)
#define QSPI_WAIT_CYCLE(n)        (n<<3)

#define QSPI_DATA_LENGTH_MASK     0x07
#define QSPI_READ_REQ_LEN         0x00

GPIO_Dir gpioDir[4] = {GPIO_INPUT, GPIO_INPUT, GPIO_INPUT, GPIO_INPUT};

static int debug_printf=0, delay_cycle=QSPI_MULTI_WR_DELAY, io_Loading=DS_8MA;
static uint32_t qspi_store_base=0x90000000;
char ft4222A_desc[64];
char ft4222B_desc[64];
static const char *const short_options = "bhrVwya:B:D:d:g:l:L:p:s:S:v:";
static const struct option long_options[] = {
   {"base", no_argument, NULL, 'b'},
   {"Binary", required_argument, NULL, 'B'},
   {"help", no_argument, NULL, 'h'},
   {"addr", required_argument, NULL, 'a'},
   {"div", required_argument, NULL, 'd'},
   {"Data", required_argument, NULL, 'D'},
   {"debug", required_argument, NULL, 'g'},
   {"delay", required_argument, NULL, 'l'},
   {"Load", required_argument, NULL, 'L'},
   {"dump", required_argument, NULL, 'p'},
   {"read", no_argument, NULL, 'r'},
   {"string", required_argument, NULL, 's'},
   {"Script", required_argument, NULL, 'S'},
   {"write", no_argument, NULL, 'w'},
   {"Version", no_argument, NULL, 'V'},
   {"voltage", required_argument, NULL, 'v'},
   {"verify", no_argument, NULL, 'y'},
   {NULL, no_argument, NULL, 0},
};

static void print_usage(FILE * stream, char *app_name, int exit_code)
{
   fprintf(stream, "Usage: %s %s-%s [options]\n", app_name, FT4222_QSPI_TOOL_GIT_TAG, FT4222_QSPI_TOOL_GIT_COMMIT);
   fprintf(stream,
      " -a  --addr <address>      Setting QSPI access address.\n"
      " -b  --base                Display SPI2AHB Base Address.\n"
      " -B  --Binary <file>       QSPI Write with binary file.\n"
      " -d  --div <division>      Setting QSPI CLOCK with 80MHz/<division>.\n"
      " -D  --Data <value>        Setting QSPI Send data value.\n"
      " -g  --debug               Display QSPI W/R Send Data Info.\n"
      " -h  --help                Display this usage information.\n"
	  " -l  --delay               Setting QSPI CMD Send Operation Delay.\n"
      " -p  --dump <size>         Dump Address size Context.\n"
	  " -r  --read                Setting QSPI Read Operation.\n"
      " -s  --string <string>     QSPI Write with string.\n"
      " -S  --Script <text file>  QSPI Write with file context.\n"
      " -w  --write               Setting QSPI Write Operation.\n"
      " -V  --Version             Display FT4222 Chip version and LibFT4222 version.\n"
      " -v  --voltage             Setting QSPI IO voltage .\n"
      " -y  --verify              Verfiy QSPI Write binary file.\n");
 
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

static void show_progress_bar(int cnt)
{
	printf("%3d%%\n",cnt);
	printf("\033[1A");
	printf("\r");
	return;
}

const char * replace(
    char * original,
    char const * const pattern,
    char const * const replacement
) {
  size_t const replen = strlen(replacement);
  size_t const patlen = strlen(pattern);
  size_t const orilen = strlen(original);

  size_t patcnt = 0;
  const char * oriptr;
  const char * patloc;

  // find how many times the pattern occurs in the original string
  for (oriptr = original; patloc = strstr(oriptr, pattern); oriptr = patloc + patlen)
  {
    patcnt++;
  }

  {
    // allocate memory for the new string
    size_t const retlen = orilen + patcnt * (replen - patlen);
    char * const returned = (char *) malloc( sizeof(char) * (retlen + 1) );

    if (returned != NULL)
    {
      // copy the original string,
      // replacing all the instances of the pattern
      char * retptr = returned;
      for (oriptr = original; patloc = strstr(oriptr, pattern); oriptr = patloc + patlen)
      {
        size_t const skplen = patloc - oriptr;
        // copy the section until the occurence of the pattern
        strncpy(retptr, oriptr, skplen);
        retptr += skplen;
        // copy the replacement
        strncpy(retptr, replacement, replen);
        retptr += replen;
      }
      // copy the rest of the string.
      strcpy(retptr, oriptr);
    }
    strcpy(original,returned);
    free (returned);
    return original;
  }
}

static int hex2data(unsigned char *data, const unsigned char *hexstring, unsigned int len)
{
    unsigned const char *pos = hexstring;
    char *endptr;
    size_t count = 0;

    if ((hexstring[0] == '\0') || (strlen(hexstring) % 2)) {
        //hexstring contains no data
        //or hexstring has an odd length
        return -1;
    }

    for(count = 0; count < len; count++) {
        char buf[5] = {'0', 'x', pos[0], pos[1], 0};
        data[count] = strtol(buf, &endptr, 0);
        pos += 2 * sizeof(char);

        if (endptr[0] != '\0') {
            //non-hexadecimal character encountered
            return -1;
        }
    }
    return 0;
}

static int removeScriptComments(char *s, char CommentChar) {
 int i;
 while(*s){
   //if ((*s=='/') || (*s=='#') || (*s==0xA) || (*s==0xD) || (*s==0x0)) {
   if ((*s=='/') || (*s==CommentChar) || (*s==0xA) || (*s==0xD) || (*s==0x0)) {
     *s=0;
     break;
   }
   s++;
 }
 return 0;
}

static int get_file_size(char *filename)
{
	struct stat st;
	if (stat(filename, &st) == 0)
		return st.st_size;
	return -1;
}

static void showVersion(FT_HANDLE ftHandle, char *desc)
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
		printf("\nDevice: '%s'\n",desc);
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

static void IOx_Index_SetOut(FT_HANDLE ftHandle_B, uint8_t bIOx_Index)
{
	FT4222_STATUS  ft4222Status;

	gpioDir[bIOx_Index] = GPIO_OUTPUT;
	ft4222Status = FT4222_GPIO_Init(ftHandle_B, &gpioDir[0]);
}

void IOx_Index_SetValue(FT_HANDLE ftHandle_B, uint8_t bIOx_Index, int iValue)
{
	FT4222_STATUS  ft4222Status;
	GPIO_Port GPIO_PortX = GPIO_PORT2;

	switch (bIOx_Index)
	{
		case 0x02:
			GPIO_PortX = GPIO_PORT2;
			break;

		case 0x03:
			GPIO_PortX = GPIO_PORT3;
			break;

		default:
			break;
	}
	BOOL bIO_Value = (iValue == 0) ? FALSE : TRUE;
	ft4222Status = FT4222_GPIO_Write(ftHandle_B, GPIO_PortX, bIO_Value);
}

void Config_Init(FT_HANDLE ftHandle_B)
{
	IOx_Index_SetOut(ftHandle_B,0x03);
	IOx_Index_SetValue(ftHandle_B,0x03, 1);
	FT4222_I2CMaster_Init(ftHandle_B, 100);
}

BOOL Config_Set_VIO_2(FT_HANDLE ftHandle_B, uint16_t iValue)
        {
            uint16_t iSizeTransferred = 0;
            uint8_t writeBuffer[3];

            writeBuffer[0] = 0x60;
            writeBuffer[1] = (uint8_t)(iValue >> 8);
            writeBuffer[2] = (uint8_t)(iValue & 0xFF);

            if (FT4222_OK == FT4222_I2CMaster_Write(ftHandle_B, (uint16_t)0x61, writeBuffer, (uint16_t)3, &iSizeTransferred))
                return (TRUE);

            return (FALSE);
        }

BOOL Config_Set_VIO(FT_HANDLE ftHandle_B, double VIO)
        {
            Config_Init(ftHandle_B);

            uint16_t iValue = (uint16_t)(((2 * VIO - 3.3) / 3.3) * 65535);
            BOOL bValue = Config_Set_VIO_2(ftHandle_B, iValue);

            return (bValue);
        }

static uint8_t ft4222_qspi_get_read_status(FT_HANDLE ftHandle)
{
	uint8_t cmd[4]    = {0};
	uint8_t buffer[4] = {0};
	FT4222_STATUS  ft4222Status;
	uint32_t sizeOfRead;

    //Send Read Status
	cmd[0] = QSPI_READ_OP | QSPI_TRANS_STATUS | QSPI_WAIT_CYCLE(0);
	ft4222Status = FT4222_SPIMaster_MultiReadWrite(
						ftHandle,
						buffer, //readBuffer
						cmd, //writeBuffer
						0, //singleWriteBytes = 0
						1, //multiWriteBytes
						1, //multiReadBytes = 0
						&sizeOfRead);
	msleep(1);
	if (debug_printf == 's') {
		printf("Get Status cmd:%02x\n",cmd[0]);
		printf("Get Status:%02x\n",buffer[0]);
		printf("\n");
	}

    return buffer[0];
}

static uint8_t ft4222_qspi_get_write_status(FT_HANDLE ftHandle)
{
	uint8_t cmd[4]    = {0};
	uint8_t buffer[4] = {0};
	FT4222_STATUS  ft4222Status;
	uint32_t sizeOfRead;

    //Send Read Status
	cmd[0] = QSPI_WRITE_OP | QSPI_TRANS_STATUS | QSPI_WAIT_CYCLE(0);
	ft4222Status = FT4222_SPIMaster_MultiReadWrite(
						ftHandle,
						buffer, //readBuffer
						cmd, //writeBuffer
						0, //singleWriteBytes = 0
						1, //multiWriteBytes
						1, //multiReadBytes = 0
						&sizeOfRead);
	msleep(1);
	if (debug_printf == 's') {
		printf("Get Status cmd:%02x\n",cmd[0]);
		printf("Get Status:%02x\n",buffer[0]);
		printf("\n");
	}

    return buffer[0];
}


static int ft4222_qspi_write_nword(FT_HANDLE ftHandle, unsigned int offset, uint8_t *buffer, uint16_t bytes)
{
    int success = 1, row = 0 ,delay_cnt = 1 ,retry_times = 0;
	uint8_t *writeBuffer =  NULL;
	uint8_t cmd[4]= {0};
	uint8_t data_length,w_status = 0x0;
	FT4222_STATUS  ft4222Status = FT4222_OK;
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
			success = 0;
			goto exit;
	}

	delay_cnt = bytes/QSPI_DUMP_WORD;
	cmd[0] = QSPI_WRITE_OP | QSPI_TRANS_DATA | data_length;
	cmd[1] = (offset >> 18) & 0xFF;
	cmd[2] = (offset >> 10) & 0xFF;
	cmd[3] = (offset >> 2) & 0xFF;

	writeBuffer = malloc(sizeof(cmd) + bytes);

	memset(writeBuffer,0x0,sizeof(cmd) + bytes);
	memcpy(writeBuffer, cmd, sizeof(cmd));
	memcpy(writeBuffer + sizeof(cmd), buffer, bytes);

	if (debug_printf == 'w') {
		printf("[QSPI Write OP]\n");
		printf("[CMD:%d bytes]\n",(int)sizeof(cmd));
		for(row=0;row < sizeof(cmd); row++ )
			printf("%02x ", *(writeBuffer + row));
		printf("\n");

		printf("[DATA:%d bytes]\n",bytes);
		for(row=0;row < bytes; row++ )
		{
			if ((row%16 == 0) && (row > 0))
				printf("\n");
			printf("%02x ", *(writeBuffer + sizeof(cmd) + row));
		}

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
	//msleep(delay_cnt*delay_cycle);
	msleep(delay_cycle);

    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPIMaster_MultiReadWrite failed (error %d)!\n",
               ft4222Status);
        success = 0;
        goto exit;
    }

	#if QSPI_STATUS_ENABLE
	while(!(QSPI_WR_READY & (w_status = ft4222_qspi_get_read_status(ftHandle))))
	{
		retry_times ++;
		if (retry_times > QSPI_MULTI_WR_RETRY)
		{
			printf("ft4222_qspi_get_write_status retry tiemout w_status %02x!\n",
				   w_status);
			success = 0;
			goto exit;
		}
		//msleep(delay_cnt*delay_cycle);
		msleep(delay_cycle);
	}
	#endif
exit:
	free(writeBuffer);
    return success;
}

static int ft4222_qspi_read_nword(FT_HANDLE ftHandle, unsigned int offset, uint8_t *buffer, uint16_t bytes)
{
    int success = 1 ,cnt = 0,delay_cnt = 1 ,retry_times = 0;;
	uint8_t cmd[4]= {0};
	uint8_t data_length, r_status = 0x0;
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

	delay_cnt = bytes/QSPI_DUMP_WORD;
	//Send Read Request
	cmd[0] = QSPI_READ_OP | QSPI_READ_REQUEST | data_length;
	cmd[1] = (offset >> 18) & 0xFF;
	cmd[2] = (offset >> 10) & 0xFF;
	cmd[3] = (offset >> 2) & 0xFF;

	if (debug_printf == 'r') {
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
	//msleep(delay_cnt*delay_cycle);
	msleep(delay_cycle);

    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPIMaster_MultiReadWrite failed (error %d)!\n",
               ft4222Status);
        success = 0;
        goto exit;
    }

	#if QSPI_STATUS_ENABLE
	while(!(QSPI_WR_READY & (r_status = ft4222_qspi_get_read_status(ftHandle))))
	{
		retry_times ++;
		if (retry_times > QSPI_MULTI_WR_RETRY)
		{
			printf("ft4222_qspi_get_read_status retry tiemout r_status %02x!\n",
				   r_status);
			success = 0;
			goto next;
		}
		//msleep(delay_cnt*delay_cycle);
		msleep(delay_cycle);
	}
	#endif
next:
    //Send Read Data
	cmd[0] = QSPI_READ_OP | QSPI_TRANS_DATA | QSPI_WAIT_CYCLE(0) | data_length;
	ft4222Status = FT4222_SPIMaster_MultiReadWrite(
						ftHandle,
						buffer, //readBuffer
						cmd, //writeBuffer
						0, //singleWriteBytes = 0
						1, //multiWriteBytes
						bytes, //multiReadBytes = 0
						&sizeOfRead);

	//msleep(delay_cnt*delay_cycle);
	msleep(delay_cycle);

    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPIMaster_MultiReadWrite failed (error %d)!\n",
               ft4222Status);
        success = 0;
        goto exit;
    }

	if (debug_printf == 'r') {
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
	uint32_t set_base_addr  =(mem_addr/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;

	if (qspi_store_base != set_base_addr)
	{
		if (!ft4222_qspi_get_base(ftHandle, &qspi_base_addr))
		{
			printf("Failed to ft4222_qspi_get_base.\n");
			success = 0;
			goto exit;
		}
		//printf("qspi_store_base: %08x  qspi_base_addr:%08x set_base_addr:%08x.\n",qspi_store_base,qspi_base_addr,set_base_addr);
	}
	else
		goto exit;


retry_switch:

	// Check QSPI Base Address
	if (qspi_base_addr != set_base_addr)
	{
		qspi_base[0] = (set_base_addr >> 24) & 0xFF;
		qspi_base[1] = (set_base_addr >> 16) & 0xFF;
		qspi_base[2] = (set_base_addr >>  8) & 0xFF;
		qspi_base[3] = (set_base_addr >>  0) & 0xFF;

		if (!ft4222_qspi_write_nword(ftHandle, QSPI_SET_BASE_ADDR, qspi_base, sizeof(qspi_base)))
		{
			printf("Failed switch base address to 0x%8x.\n",set_base_addr);
			success = 0;
			goto exit;
		}
		msleep(delay_cycle);

		if (!ft4222_qspi_get_base(ftHandle, &qspi_base_addr))
		{
			printf("Failed to ft4222_qspi_get_base.\n");
			success = 0;
			goto exit;
		}

		if ((qspi_base_addr != set_base_addr) && (retry < 3)) {
			retry ++;
			goto retry_switch;
		}

		if (retry >= 3) {
			printf("Failed to retry switch new base 0x%08x.\n",set_base_addr);
			success = 0;
			goto exit;
		}
	}

exit:
	qspi_store_base = set_base_addr;
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
    int success = 1, row =0, col =0, max_row = 0, max_col =0, malloc_len =0;
	uint8_t *buffer = NULL;
	uint32_t start_base  =(mem_addr/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;
	uint32_t end_base  =((mem_addr + size)/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;

	if (size > QSPI_CMD_READ_MAX) {
        printf("QSPI CMD Dump size %d exceed max %d.\n",(int)size ,QSPI_CMD_READ_MAX);
		success = 0;
        goto exit;
	}

	if ((start_base != end_base) && ((mem_addr + size)%QSPI_ACCESS_WINDOW)){
        printf("QSPI CMD Dump from 0x%08x to 0x%08x, exceed next 32M windows 0x%08x.\n",mem_addr, (mem_addr + size), end_base);
		success = 0;
        goto exit;
	}

	if (size <= 4)
		malloc_len = 4;
	else if ((size > 4) && (size <= 16))
		malloc_len = 16;
	else if ((size > 16) && (size <= 32))
		malloc_len = 32;
	else if ((size > 32) && (size <= 64))
		malloc_len = 64;
	else if ((size > 64) && (size <= 128))
		malloc_len = 128;
	else if ((size > 128) && (size <= 256))
		malloc_len = 256;
	else {
		printf("QSPI Write with string length %d exceed max 256.\n",size);
		success = 0;
		goto exit;
	}

	buffer = malloc(malloc_len);
	memset(buffer,0x0,malloc_len);

	if (!ft4222_qspi_memory_read(ftHandle, mem_addr, buffer, malloc_len))
	{
        printf("Failed to ft4222_qspi_memory_read 4 bytes.\n");
		success = 0;
        goto exit;
	}

	max_row = size/(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD);

	if (max_row) {

		for(row=0; row < max_row; row++)
		{
			printf("%08x : ", mem_addr + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM));
			for(col=0; col < QSPI_DUMP_COL_NUM; col++)
			{
				printf("%08x ", swapLong(*((uint32_t *)(buffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD))));
			}
			printf("\n");
		}

		if (size%(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD))
		{
			row++;
			max_col = (size%(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD))/QSPI_DUMP_WORD;
			printf("%08x : ", mem_addr + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM));
			for(col=0; col < max_col; col++)
			{
				printf("%08x ", swapLong(*((uint32_t *)(buffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD))));
			}
			printf("\n");
		}
	}
	else
	{
		printf("%08x : ", mem_addr);
		max_col = (size%(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD))/QSPI_DUMP_WORD;
		for(col=0; col < max_col; col++)
		{
			printf("%08x ", swapLong(*((uint32_t *)(buffer + col*QSPI_DUMP_WORD))));
		}
		printf("\n");
	}
exit:
    return success;
}

static int ft4222_qspi_cmd_read(FT_HANDLE ftHandle, uint32_t mem_addr, uint8_t *buffer, uint16_t size, int swap_word)
{
    int success = 1, row =0, col =0, max_row = 0, max_col =0, malloc_len =0;
	uint8_t *readbuffer = NULL;
	uint32_t start_base  =(mem_addr/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;
	uint32_t end_base  =((mem_addr + size)/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;

	if (size > QSPI_CMD_READ_MAX) {
        printf("QSPI CMD Dump size %d exceed max %d.\n",(int)size ,QSPI_CMD_READ_MAX);
		success = 0;
        goto exit;
	}

	if ((start_base != end_base) && ((mem_addr + size)%QSPI_ACCESS_WINDOW)){
        printf("QSPI CMD Dump from 0x%08x to 0x%08x, exceed next 32M windows 0x%08x.\n",mem_addr, (mem_addr + size), end_base);
		success = 0;
        goto exit;
	}

	if (size <= 4)
		malloc_len = 4;
	else if ((size > 4) && (size <= 16))
		malloc_len = 16;
	else if ((size > 16) && (size <= 32))
		malloc_len = 32;
	else if ((size > 32) && (size <= 64))
		malloc_len = 64;
	else if ((size > 64) && (size <= 128))
		malloc_len = 128;
	else if ((size > 128) && (size <= 256))
		malloc_len = 256;
	else {
		printf("QSPI Write with string length %d exceed max 256.\n",size);
		success = 0;
		goto exit;
	}

	readbuffer = malloc(malloc_len);
	memset(readbuffer,0x0,malloc_len);

	if (!ft4222_qspi_memory_read(ftHandle, mem_addr, readbuffer, malloc_len))
	{
        printf("Failed to ft4222_qspi_memory_read 4 bytes.\n");
		success = 0;
        goto exit;
	}

	max_row = size/(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD);

	if (max_row) {

		for(row=0; row < max_row; row++)
		{
			if ( debug_printf  == 'd')
				printf("%08x : ", mem_addr + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM));
			for(col=0; col < QSPI_DUMP_COL_NUM; col++)
			{
				if (swap_word)
				{
					if ( debug_printf  == 'd')
						printf("%08x ", swapLong(*((uint32_t *)(readbuffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD))));
					*((uint32_t *)(readbuffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD)) = \
					swapLong(*((uint32_t *)(readbuffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD)));
				}
			}
			if ( debug_printf  == 'd')
				printf("\n");
		}

		if (size%(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD))
		{
			max_col = (size%(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD))/QSPI_DUMP_WORD;
			if ( debug_printf  == 'd')
				printf("%08x : ", mem_addr + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM));
			for(col=0; col < max_col; col++)
			{
				if (swap_word)
				{
					if ( debug_printf  == 'd')
						printf("%08x ", swapLong(*((uint32_t *)(readbuffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD))));
					*((uint32_t *)(readbuffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD)) = \
					swapLong(*((uint32_t *)(readbuffer + (row*QSPI_DUMP_WORD*QSPI_DUMP_COL_NUM) + col*QSPI_DUMP_WORD)));
				}
			}
			if ( debug_printf  == 'd')
				printf("\n");
		}
	}
	else
	{
		if ( debug_printf  == 'd')
			printf("%08x : ", mem_addr);
		max_col = (size%(QSPI_DUMP_COL_NUM*QSPI_DUMP_WORD))/QSPI_DUMP_WORD;
		for(col=0; col < max_col; col++)
		{
			if (swap_word)
			{
				if ( debug_printf  == 'd')
					printf("%08x ", swapLong(*((uint32_t *)(readbuffer + col*QSPI_DUMP_WORD))));
				*((uint32_t *)(readbuffer + col*QSPI_DUMP_WORD)) = \
				swapLong(*((uint32_t *)(readbuffer + col*QSPI_DUMP_WORD)));
			}
		}
		if ( debug_printf  == 'd')
			printf("\n");
	}

	memcpy(buffer, readbuffer, size);
exit:
    return success;
}

static int ft4222_qspi_cmd_write(FT_HANDLE ftHandle, uint32_t mem_addr, uint8_t *buffer, uint16_t size, int swap_word)
{
    int success = 1;
	int malloc_len, cnt;
	uint32_t start_base  =(mem_addr/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;
	uint32_t end_base  =((mem_addr + size)/QSPI_ACCESS_WINDOW) * QSPI_ACCESS_WINDOW;
	uint32_t swap_tmp;
	uint8_t *bufPtr = NULL;

	if (size > QSPI_CMD_WRITE_MAX) {
        printf("QSPI CMD Write size %d exceed max %d.\n",(int)size ,QSPI_CMD_WRITE_MAX);
		success = 0;
        goto exit;
	}

	if ((start_base != end_base) && ((mem_addr + size)%QSPI_ACCESS_WINDOW)){
        printf("QSPI CMD Dump from 0x%08x to 0x%08x, exceed next 32M windows 0x%08x.\n",mem_addr, (mem_addr + size), end_base);
		success = 0;
        goto exit;
	}

	if (size <= 4)
		malloc_len = 4;
	else if ((size > 4) && (size <= 16))
		malloc_len = 16;
	else if ((size > 16) && (size <= 32))
		malloc_len = 32;
	else if ((size > 32) && (size <= 64))
		malloc_len = 64;
	else if ((size > 64) && (size <= 128))
		malloc_len = 128;
	else if ((size > 128) && (size <= 256))
		malloc_len = 256;
	else {
		printf("QSPI Write with string length %d exceed max 256.\n",size);
		success = 0;
		goto exit;
	}

	bufPtr  = malloc(malloc_len);
	memset(bufPtr,0x0,malloc_len);
	memcpy(bufPtr,buffer,size);

	if (swap_word)
	{
		for(cnt =0; cnt < malloc_len/QSPI_DUMP_WORD;cnt++)
		{
			swap_tmp = *((uint32_t *)(bufPtr + cnt*QSPI_DUMP_WORD));
			*((uint32_t *)(bufPtr + cnt*QSPI_DUMP_WORD)) = swapLong(swap_tmp);
		}
	}

	if (!ft4222_qspi_memory_write(ftHandle, mem_addr, bufPtr, malloc_len))
	{
        printf("Failed to ft4222_qspi_memory_read 4 bytes.\n");
		success = 0;
        goto exit;
	}
	msleep(delay_cycle);
exit:
    return success;
}

static int ft4222_qspi_memory_dump(FT_HANDLE ftHandle, uint32_t mem_addr, uint16_t size)
{
    int success = 1, cmd_time = 0, max_cmd_times = 0;
	uint32_t qspi_addr = 0;

	if (size > QSPI_DUMP_MAX_SIZE) {
        printf("QSPI Dump memory size %d exceed max %d.\n",(int)size ,QSPI_DUMP_MAX_SIZE);
		success = 0;
        goto exit;
	}

	max_cmd_times = (size/QSPI_CMD_READ_MAX);

	if (max_cmd_times) {
		for (cmd_time = 0; cmd_time < max_cmd_times; cmd_time++)
		{
			qspi_addr = mem_addr + (QSPI_CMD_READ_MAX * cmd_time);

			if (!ft4222_qspi_cmd_dump(ftHandle, qspi_addr, QSPI_CMD_READ_MAX))
			{
				printf("Failed to ft4222_qspi_cmd_dump.\n");
				success = 0;
				goto exit;
			}
			msleep(delay_cycle);
		}

		if (size%QSPI_CMD_READ_MAX)
		{
			qspi_addr = mem_addr + (QSPI_CMD_READ_MAX * cmd_time);

			if (!ft4222_qspi_cmd_dump(ftHandle, qspi_addr, size%QSPI_CMD_READ_MAX))
			{
				printf("Failed to ft4222_qspi_cmd_dump.\n");
				success = 0;
				goto exit;
			}
		}
	}
	else
	{
		qspi_addr = mem_addr;

		if (!ft4222_qspi_cmd_dump(ftHandle, qspi_addr, size))
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

static int ft4222_qspi_memory_write_string(FT_HANDLE ftHandle, uint32_t mem_addr, char *strbuf)
{
    int success = 1;
	int data_len;
	uint8_t *bufPtr = NULL;

	data_len = strlen(strbuf)/2;

	if (data_len > QSPI_CMD_WRITE_MAX) {
        printf("QSPI CMD Write size %d exceed max %d.\n",(int)data_len ,QSPI_CMD_WRITE_MAX);
		success = 0;
        goto exit;
	}

	bufPtr  = malloc(data_len);
	memset(bufPtr,0x0,data_len);
	hex2data(bufPtr,strbuf,data_len);

	if (!ft4222_qspi_cmd_write(ftHandle, mem_addr, bufPtr, data_len, QSPI_NO_SWAP_WORD))
	{
		printf("Failed to ft4222_qspi_cmd_write.\n");
		success = 0;
		goto exit;
	}
	msleep(delay_cycle);
exit:
	if (bufPtr != NULL)
		free(bufPtr);
    return success;
}


static int ft4222_qspi_memory_write_scriptfile(FT_HANDLE ftHandle, uint32_t mem_addr, char *script_name)
{
    int success = 1;
	int data_len, malloc_len, cmd_time, max_cmd_times, process_times;
	uint32_t qspi_addr = 0;
	size_t filesize;
	char *buf_script =NULL;
	uint8_t *bufPtr = NULL;
    FILE *fp_script;

    fp_script =fopen(script_name,"r");
	if (!fp_script)
	{
		printf("cannot open file: %s \n",script_name);
		success = 0;
		goto exit;
	}

	filesize = get_file_size(script_name);
	buf_script = malloc(filesize);

	fread(buf_script, sizeof(char), filesize, fp_script);
	strcpy(buf_script,replace(buf_script," ",""));
	removeScriptComments(buf_script, '#');
	fclose(fp_script);
	//printf("-S len %d %s \n", (int)strlen(buf_script)/2, buf_script);

	data_len = strlen(buf_script)/2;
	bufPtr  = malloc(data_len);
	memset(bufPtr,0x0,data_len);
	hex2data(bufPtr,buf_script,data_len);

	max_cmd_times = (data_len/QSPI_CMD_WRITE_MAX);
	process_times = (data_len%QSPI_CMD_WRITE_MAX) ? (max_cmd_times + 1) : max_cmd_times;

	if (max_cmd_times) {
		for (cmd_time = 0; cmd_time < max_cmd_times ; cmd_time++)
		{
			qspi_addr = mem_addr + (QSPI_CMD_WRITE_MAX * cmd_time);

			if (!ft4222_qspi_cmd_write(ftHandle, qspi_addr, bufPtr + (QSPI_CMD_WRITE_MAX * cmd_time), QSPI_CMD_WRITE_MAX, QSPI_NO_SWAP_WORD))
			{
				printf("Failed to ft4222_qspi_cmd_write.\n");
				success = 0;
				goto exit;
			}
			show_progress_bar((cmd_time*100)/process_times);
			msleep(delay_cycle);
		}

		if (data_len%QSPI_CMD_WRITE_MAX)
		{
			qspi_addr = mem_addr + (QSPI_CMD_WRITE_MAX * cmd_time);

			if (!ft4222_qspi_cmd_write(ftHandle, qspi_addr, bufPtr + (QSPI_CMD_WRITE_MAX * cmd_time), data_len%QSPI_CMD_WRITE_MAX, QSPI_NO_SWAP_WORD))
			{
				printf("Failed to ft4222_qspi_cmd_write.\n");
				success = 0;
				goto exit;
			}
		}
		show_progress_bar(100);
	}
	else
	{
		qspi_addr = mem_addr;

		if (!ft4222_qspi_cmd_write(ftHandle, qspi_addr, bufPtr, data_len, QSPI_NO_SWAP_WORD))
		{
			printf("Failed to ft4222_qspi_cmd_write.\n");
			success = 0;
			goto exit;
		}
	}

exit:
    return success;
}

static int ft4222_qspi_memory_write_binaryfile(FT_HANDLE ftHandle, uint32_t mem_addr, char *binary_file)
{
    int success = 1, cmd_time = 0, max_cmd_times = 0, process_times = 0;
	uint32_t qspi_addr = 0;
	size_t filesize;
	uint8_t *bufPtr = NULL;
    FILE *fp_binary;

    fp_binary =fopen(binary_file,"rb");
	if (!fp_binary)
	{
		printf("cannot open file: %s \n",binary_file);
		success = 0;
		goto exit;
	}

	filesize = get_file_size(binary_file);
	bufPtr = malloc(filesize);
	memset(bufPtr,0x0,filesize);
	fread(bufPtr, sizeof(char), filesize, fp_binary);
	fclose(fp_binary);

	max_cmd_times = (filesize/QSPI_CMD_WRITE_MAX);
	process_times = ((filesize%QSPI_CMD_WRITE_MAX) != 0) ? (max_cmd_times + 1) : max_cmd_times ;

	//printf("%s filesize %d \n",binary_file,(int)filesize);
	//printf("max_cmd_times %d \n",max_cmd_times);
	//printf("process_times %d \n",process_times);

	if (max_cmd_times) {
		for (cmd_time = 0; cmd_time < max_cmd_times; cmd_time++)
		{
			qspi_addr = mem_addr + (QSPI_CMD_WRITE_MAX * cmd_time);

			if (!ft4222_qspi_cmd_write(ftHandle, qspi_addr, bufPtr + (QSPI_CMD_WRITE_MAX * cmd_time), QSPI_CMD_WRITE_MAX , QSPI_SWAP_WORD))
			{
				printf("%s line%d:Failed to ft4222_qspi_cmd_write address 0x%08x.\n",__func__,__LINE__,qspi_addr);
				success = 0;
				goto exit;
			}
			show_progress_bar((cmd_time*100)/process_times);
			msleep(delay_cycle);
		}

		if (filesize%QSPI_CMD_WRITE_MAX)
		{
			qspi_addr = mem_addr + (QSPI_CMD_WRITE_MAX * cmd_time);

			if (!ft4222_qspi_cmd_write(ftHandle, qspi_addr, bufPtr + (QSPI_CMD_WRITE_MAX * cmd_time), filesize%QSPI_CMD_WRITE_MAX, QSPI_SWAP_WORD))
			{
				printf("%s line%d:Failed to ft4222_qspi_cmd_write address 0x%08x.\n",__func__,__LINE__,qspi_addr);
				success = 0;
				goto exit;
			}
		}
		show_progress_bar(100);
	}
	else
	{
		qspi_addr = mem_addr;

		if (!ft4222_qspi_cmd_write(ftHandle, qspi_addr, bufPtr, filesize, QSPI_SWAP_WORD))
		{
			printf("%s line%d:Failed to ft4222_qspi_cmd_write address 0x%08x.\n",__func__,__LINE__,qspi_addr);
			success = 0;
			goto exit;
		}
	}

exit:
    return success;
}

static int ft4222_qspi_memory_write_binaryfile_verify(FT_HANDLE ftHandle, uint32_t mem_addr, char *binary_file)
{
    int success = 1, cmd_time = 0, max_cmd_times = 0, process_times = 0 ,bcmpcmpsize = 0;
	uint32_t qspi_addr = 0;
	size_t filesize;
	uint8_t *bufPtr = NULL, *readbufPtr = NULL;
    FILE *fp_binary;

    fp_binary =fopen(binary_file,"rb");
	if (!fp_binary)
	{
		printf("cannot open file: %s \n",binary_file);
		success = 0;
		goto exit;
	}

	filesize = get_file_size(binary_file);
	bufPtr = malloc(filesize);
	memset(bufPtr,0x0,filesize);
	fread(bufPtr, sizeof(char), filesize, fp_binary);
	fclose(fp_binary);

	max_cmd_times = (filesize/QSPI_CMD_READ_MAX);
	process_times = (filesize%QSPI_CMD_READ_MAX) ? (max_cmd_times + 1) : max_cmd_times;

	readbufPtr = malloc(process_times*QSPI_CMD_READ_MAX);
	memset(readbufPtr,0x0,(process_times*QSPI_CMD_READ_MAX));

	if (max_cmd_times) {
		for (cmd_time = 0; cmd_time < max_cmd_times; cmd_time++)
		{
			qspi_addr = mem_addr + (QSPI_CMD_READ_MAX * cmd_time);

			if (!ft4222_qspi_cmd_read(ftHandle, qspi_addr, readbufPtr + (QSPI_CMD_READ_MAX * cmd_time), QSPI_CMD_READ_MAX , QSPI_SWAP_WORD))
			{
				printf("%s line%d:Failed to ft4222_qspi_cmd_write address 0x%08x.\n",__func__,__LINE__,qspi_addr);
				success = 0;
				goto exit;
			}
		}

		if (filesize%QSPI_CMD_READ_MAX)
		{
			qspi_addr = mem_addr + (QSPI_CMD_READ_MAX * cmd_time);

			if (!ft4222_qspi_cmd_read(ftHandle, qspi_addr, readbufPtr + (QSPI_CMD_READ_MAX * cmd_time), filesize%QSPI_CMD_READ_MAX, QSPI_SWAP_WORD))
			{
				printf("%s line%d:Failed to ft4222_qspi_cmd_write address 0x%08x.\n",__func__,__LINE__,qspi_addr);
				success = 0;
				goto exit;
			}
		}
	}
	else
	{
		qspi_addr = mem_addr;

		if (!ft4222_qspi_cmd_read(ftHandle, qspi_addr, readbufPtr, filesize, QSPI_SWAP_WORD))
		{
			printf("%s line%d:Failed to ft4222_qspi_cmd_write address 0x%08x.\n",__func__,__LINE__,qspi_addr);
			success = 0;
			goto exit;
		}
	}

	bcmpcmpsize = bcmp(bufPtr,readbufPtr,filesize);

	if (bcmpcmpsize != 0)
	{
		printf("%s: bcmp %dbytes are different\n",__func__, (bcmpcmpsize < 0) ? (-bcmpcmpsize) : bcmpcmpsize);
		printf("Verify: NK\n");
		success = 0;
		goto exit;
	}
	printf("Verify: OK\n");
exit:
	if (bufPtr != NULL)
		free(bufPtr);
	if (readbufPtr != NULL)
		free(readbufPtr);
    return success;
}

int main(int argc, char **argv)
{
   int division = QSPI_DEFAULT_DIV,write_op = 0, read_op = 0,
       addr_set = 0, data_set = 0, show_base = 0,
	   show_ft4222_ver = 0, dump_show = 0, dump_size = 0,
	   string_send = 0, script_send = 0, binary_send = 0,
	   i = 0, retCode = 0, found4222 = 0, ioVoltage_set = 0, verify_set = 0,
	   next_option;  /* getopt iteration var */
   double                    ft4222IOVoltage = 1.8;
   FT_STATUS                 ftStatus;
   FT4222_STATUS             ft4222Status;
   FT_HANDLE                 ft4222AHandle = (FT_HANDLE)NULL;
   FT_HANDLE                 ft4222BHandle = (FT_HANDLE)NULL;
   FT_DEVICE_LIST_INFO_NODE  *devInfo = NULL;
   FT4222_SPIClock           ftQspiClk = ft4222_convert_qspiclk(division); //Set QSPI CLK default CLK_DIV_128 80M/128=625Khz
   DWORD                     numDevs = 0;
   DWORD                     ft4222A_LocId;
   DWORD                     ft4222B_LocId;
   size_t                    strLength;
   char                      *strbuf = NULL;
   char                      *scriptFile= NULL, *binaryFile= NULL;
   unsigned int              addr,spi2ahb_base,data_value,tmp_value = 0x0;

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
				ft4222A_LocId = devInfo[i].LocId;
				strcpy(ft4222A_desc, devInfo[i].Description);
            }
            else if ('B' == devInfo[i].Description[descLen - 1])
            {
                // Interface B, C or D.
                ft4222B_LocId = devInfo[i].LocId;
				strcpy(ft4222B_desc, devInfo[i].Description);
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
	  case 'B':
			strLength = strlen(optarg);
			binaryFile = malloc(strLength);
			strcpy(binaryFile,replace(optarg," ",""));
			binary_send = 1;
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
			debug_printf = atoi(optarg);
         break;
      case 'l':
			delay_cycle = atoi(optarg);
         break;
      case 'L':
			io_Loading = atoi(optarg);
			if ((io_Loading < DS_4MA) || (io_Loading > DS_16MA))
			{
				printf("io_Loading setting %d is not @DS_4MA ~ DS_16MA\n",io_Loading);
				print_usage(stderr, argv[0], EXIT_FAILURE);
			}
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
      case 's':
			strLength = strlen(optarg);
			strbuf = malloc(strLength);
			strcpy(strbuf,replace(optarg," ",""));
			string_send = 1;
			//printf("-s %s \n",strbuf);
         break;
      case 'S':
			strLength = strlen(optarg);
			scriptFile = malloc(strLength);
			strcpy(scriptFile,replace(optarg," ",""));
			script_send = 1;
         break;
      case 'V':
			show_ft4222_ver = 1;
		break;
      case 'v':
			ft4222IOVoltage = atof(optarg);
			printf("\nSetting QSPI IO Voltage %f done\n",(double)ft4222IOVoltage);
			ioVoltage_set = 1;
		break;
      case 'w':
			write_op = 1;
         break;
      case 'y':
			verify_set = 1;
         break;
      case '?':   /* Invalid options */
         print_usage(stderr, argv[0], EXIT_FAILURE);
      case -1:   /* Done with options */
         break;
      default:   /* Unexpected stuffs */
         abort();
      }
   } while (next_option != -1);

    ftStatus = FT_OpenEx((PVOID)(uintptr_t)ft4222A_LocId,
                         FT_OPEN_BY_LOCATION,
                         &ft4222AHandle);
    if (ftStatus != FT_OK)
    {
        printf("FT_OpenEx failed (error %d)\n",
               (int)ftStatus);
		retCode = ftStatus;
        goto exit;
    }

    ftStatus = FT_OpenEx((PVOID)(uintptr_t)ft4222B_LocId,
                         FT_OPEN_BY_LOCATION,
                         &ft4222BHandle);
    if (ftStatus != FT_OK)
    {
        printf("FT_OpenEx failed (error %d)\n",
               (int)ftStatus);
		retCode = ftStatus;
        goto exit;
    }

	if (ioVoltage_set)
	{
		Config_Set_VIO(ft4222BHandle, ft4222IOVoltage);
	}

	if (show_ft4222_ver)
	{
		printf("%s %s-%s\n", argv[0], FT4222_QSPI_TOOL_GIT_TAG, FT4222_QSPI_TOOL_GIT_COMMIT);
		showVersion(ft4222AHandle,ft4222A_desc);
		showVersion(ft4222BHandle,ft4222B_desc);
	}

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

    if (string_send)
    {
	    if (addr_set == 0)
	    {
			printf("ft4222 work in string write mode,addr is missing\n");
			retCode = -30;
			goto ft4222_exit;
	    }
    }

    if (script_send)
    {
	    if (addr_set == 0)
	    {
			printf("ft4222 work in script file write mode,addr is missing\n");
			retCode = -30;
			goto ft4222_exit;
	    }
    }

    if (binary_send)
    {
	    if (addr_set == 0)
	    {
			printf("ft4222 work in binary file write mode,addr is missing\n");
			retCode = -30;
			goto ft4222_exit;
	    }
    }

    if (verify_set && binary_send)
    {
	    if (addr_set == 0)
	    {
			printf("ft4222 work in binary file verfiy mode,addr is missing\n");
			retCode = -30;
			goto ft4222_exit;
	    }
    }

    // Configure the FT4222 as an SPI Master.
    ft4222Status = FT4222_SPIMaster_Init(
                        ft4222AHandle,
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

    ft4222Status = FT4222_SPI_SetDrivingStrength(ft4222AHandle,
                                                 io_Loading,
                                                 io_Loading,
                                                 io_Loading);
    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPI_SetDrivingStrength failed (error %d)\n",
               (int)ft4222Status);
        goto ft4222_exit;
    }

	if (show_base) {
		ft4222_qspi_get_base(ft4222AHandle, &tmp_value);
		printf("QSPI2AHB Current Base Address 0x%08x\n", tmp_value);
	}
	if (write_op) {
		ft4222_qspi_memory_write_word(ft4222AHandle, addr, data_value);
	}

	if (string_send) {
		ft4222_qspi_memory_write_string(ft4222AHandle, addr, strbuf);
	}

	if (script_send) {
		ft4222_qspi_memory_write_scriptfile(ft4222AHandle, addr, scriptFile);
	}

	if (binary_send) {
		ft4222_qspi_memory_write_binaryfile(ft4222AHandle, addr, binaryFile);
	}

	if (read_op) {
		ft4222_qspi_memory_read_word(ft4222AHandle, addr, &tmp_value);
		printf("%08x : %08x\n", addr, tmp_value);
	}

	if (dump_show) {
		ft4222_qspi_memory_dump(ft4222AHandle, addr, dump_size);
	}

    if (verify_set && binary_send)
    {
		if ( debug_printf  == 'd')
			printf("Verify Dump Data:\n");
		ft4222_qspi_memory_write_binaryfile_verify(ft4222AHandle, addr, binaryFile);
    }

ft4222_exit:
    (void)FT_Close(ft4222AHandle);
    (void)FT_Close(ft4222BHandle);
exit:
	if (strbuf != NULL)
		free(strbuf);
    free(devInfo);
    return retCode;
}
