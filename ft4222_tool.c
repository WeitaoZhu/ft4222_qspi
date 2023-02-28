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

// SPI Master can assert SS0O in single mode
// SS0O and SS1O in dual mode, and
// SS0O, SS1O, SS2O and SS3O in quad mode.
#define SLAVE_SELECT(x)      (1 << (x))

#define QSPI_ACCESS_WINDOW   0x02000000
#define QSPI_SET_BASE_ADDR   0x02000004

static const char *const short_options = "hva:d:i:";
static const struct option long_options[] = {
   {"help", no_argument, NULL, 'h'},
   {"version", no_argument, NULL, 'v'},
   {"addr", required_argument, NULL, 'a'},
   {"div", required_argument, NULL, 'd'},
   {"interval", required_argument, NULL, 'i'},
   {NULL, no_argument, NULL, 0},
};

static void print_usage(FILE * stream, char *app_name, int exit_code)
{
   fprintf(stream, "Usage: %s [options]\n", app_name);
   fprintf(stream,
      " -h  --help                Display this usage information.\n"
      " -v  --version             Display FT4222 Chip version and LibFT4222 version.\n"
      " -a  --Addr <address>      Setting QSPI access address.\n"
      " -d  --div <division>      Setting QSPI CLOCK with 80MHz/<division>.\n"
      " -i  --interval <interval> Change the watchdog interval time\n");
 
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
        printf("  Chip version: %08X, LibFT4222 version: %08X\n",
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

static int ft4222_qspi_switch_base(FT_HANDLE ftHandle)
{
    int success = 0;
    return success;
}

static int ft4222_qspi_write(FT_HANDLE ftHandle)
{
    int success = 0;
    return success;
}

static int ft4222_qspi_read(FT_HANDLE ftHandle)
{
    int success = 0;
    return success;
}

int main(int argc, char **argv)
{
   FT_STATUS                 ftStatus;
   FT4222_STATUS             ft4222Status;
   FT_HANDLE                 ftHandle = (FT_HANDLE)NULL;
   FT_DEVICE_LIST_INFO_NODE  *devInfo = NULL;
   FT4222_SPIClock           ftQspiClk = CLK_DIV_128;  //Set QSPI CLK default CLK_DIV_128 80M/128=625Khz
   DWORD                     numDevs = 0;
   DWORD                     ft4222_LocId;
   unsigned int              addr;
   int                       i;
   int                       retCode = 0;
   int                       found4222 = 0;	

   int fd;
   int interval,division;
   int next_option;   /* getopt iteration var */
 
   /* Init variables */
   interval = 0;
   division = 0;
   
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

   /* Parse options if any */
   do {
      next_option = getopt_long(argc, argv, short_options,
                 long_options, NULL);
      switch (next_option) {
      case 'h':
         print_usage(stdout, argv[0], EXIT_SUCCESS);
      case 'v':
		 showVersion(ftHandle);
		break;
      case 'a':
	     addr = get_ul_number(optarg);
		 if (addr > 0xFFFFFFFF) {
			 printf("FT4222 QSPI Access Invalid Address 0x%08x\n",addr);
		     goto ft4222_exit;
		 }
         break;
      case 'd':
	     division = atoi(optarg);
		 ftQspiClk = ft4222_convert_qspiclk(division);
         break;
      case 'i':
         interval = atoi(optarg);
         break;
      case '?':   /* Invalid options */
         print_usage(stderr, argv[0], EXIT_FAILURE);
      case -1:   /* Done with options */
         break;
      default:   /* Unexpected stuffs */
         abort();
      }
   } while (next_option != -1);
   
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
                                                 DS_8MA,
                                                 DS_8MA,
                                                 DS_8MA);
    if (FT4222_OK != ft4222Status)
    {
        printf("FT4222_SPI_SetDrivingStrength failed (error %d)\n",
               (int)ft4222Status);
        goto ft4222_exit;
    }

ft4222_exit:
    (void)FT_Close(ftHandle);
exit:
    free(devInfo);
    return retCode;
}
