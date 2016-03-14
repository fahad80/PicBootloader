// PIC 33F Bootloader
// Up to 256k program word devices
// Win32 Version

// Notes on HEX file format:
//
// File is in plain text
// :LLAAAATT[DD...]CC
// LL (data length)
// AAAA (lower 16-bits of address)
// TT (type: 0=data, 1=end, 2=ext seg (not supported), 4=ext linear address)
// DD (LL bytes of data)
// CC (2's compliment of all bytes (LL, AAAA, TT, DD's, and CC)
// if TT = 4, then LL = 2 and DDDD will be the upper 16-bits of address 
//   to be used for subsequent lines
// Addresses in the hex file are byte addresses, not word addresses
// A value for the unimplemented LSB in a program word in included in the file

// Note on programming the 33F:
//
// Program instructions are 3 bytes long 
// Program addresses are word addresses, not byte addresses
// Program instructions start on even word addresses (every 4th byte)
// Program memory can be written in rows consisting of 64 instructions
// Program memory can be erased in pages consisting of 8 rows (512 instructions)
// A 2048 byte block of data is transmitted to the PIC for each page
// This corresponds to 1024 word addresses of data or 512 instructions of data

// Notes on code implementation on the 33F:
//
// The bootloader code area is in page 1 (program words 0x000400 to 0x0007FF) 
// This code verifies that the hex file does not overwrite page 1
// The bootloader on the PIC will discard the goto destination at word address
//  0x000001 and replace the address with 0x000400 (boot_start)
// The code will also verify that the goto address in word address 0x000001
//  of the hex file is 0x000800
// If the bootloader determines that a code load is not needed, it will
//  goto program address 0x000800 (beginning of page 2)
// Target code for the 33F using the bootloader should makes changes to 
//  the GLD file to force the .text section to start at the beginning of 
//  page 2 (word address 0x000800)

#include "stdafx.h"
#include "string.h"
#include "windows.h"

// map to contain data read from hex file prior to transmitting to the PIC
// make this global to prevent need to change stack size
unsigned char nMap[1048576];

int main(int argc, char* argv[])
{ 
  HANDLE hFile = NULL;
  HANDLE hPort = NULL;
  DCB dcb;
  int nSize;
  unsigned int nAddrL;
  unsigned int nAddrH;
  unsigned int nAddr;
  unsigned int nMaxAddr;
  unsigned int nType;
  unsigned char nChecksum;
  unsigned char nData;
  bool bEOF = false;
  int nRecord = 0;
  int i;
  bool bOK = true;
  char strPort[7]= "COM1";
  DWORD dwCount;
  bool bCloseHyperterminal = false;
  bool bOpenHyperterminal = false;
  int nNonSwitchArguments = 0;
  int nArgIndex[20];
  int nRetryCount = 0;

  printf("\nPIC 33F Bootloader\n");

  // parse command line
  i = 0;
  while (bOK && (i < argc))
  {
    // handle switch
    if (argv[i][0] == '/')
    {
      bOK = false;
      // close hyperterminal connection if requested and window is open
      if (_stricmp(argv[i], "/h-") == 0)
      {
        bOK = true;
        bCloseHyperterminal = true;
      }
      // reopen hyperterminal connection if requested and window is open
      if (_stricmp(argv[i], "/h+") == 0)
      {
        bOK = true;
        bOpenHyperterminal = true;
      }
      // handle com port override
      if (_strnicmp(&argv[i][1], "COM", 3) == 0)
      {
        bOK = true;
        strcpy_s(strPort, &argv[i][1]);
      }
    }
    // record position of other fields
    else
      nArgIndex[nNonSwitchArguments++] = i;
    i++;
  }
  if (!bOK)
    printf("Invalid switch detected\n");

  if (bOK)
  {
    if (nNonSwitchArguments < 2)
    {
      bOK = false;
      printf("Missing argument(s)\n");
    }
  }
  if (!bOK)
  {
  	printf("Usage: loader filename.hex [/COMx] [/H-] [/H+]\n");
  	printf("         /COMx  selects a port (default is COM1)\n");
    printf("         /H-    closes a hyperterminal session before programming\n"); 
    printf("         /H+    reopens a hyperterminal session after programming\n"); 
  }

  // prepare to read HEX file
  if (bOK)
  {
	// make memory map uninitialized (NOPs)
    for (int i = 0; i < 1048576; i++)
      nMap[i] = 0xff;
    printf("Reading file... ");
    hFile = CreateFile(argv[nArgIndex[1]], GENERIC_READ, 0, NULL, OPEN_EXISTING, NULL, NULL);
    bOK = (hFile != NULL);
    if (!bOK) printf("error opening file\n");
  }

  // parse file and fill local memory map
  while (bOK && !bEOF)
  {
    bool bColon = false;
    while (bOK && !bColon)
    {
      char c;
      ReadFile(hFile, &c, 1, &dwCount, NULL);
      if (c != 10 && c != 13)
      {
        bOK = bColon = (c == ':');
      }
    }
    if (!bOK)
      printf("format error\n");
    else
    {
      nRecord++;
      char c[5];
      ReadFile(hFile, &c, 2, &dwCount, NULL);
      sscanf_s(c, "%2x", &nSize);
      nChecksum = nSize;
      ReadFile(hFile, &c, 4, &dwCount, NULL);
      sscanf_s(c, "%4x", &nAddrL);
      nChecksum += nAddrL & 0xff;
      nChecksum += (nAddrL >> 8) & 0xff;
      ReadFile(hFile, &c, 2, &dwCount, NULL);
      sscanf_s(c, "%2x", &nType);
      nChecksum += nType;
	  switch (nType)
      {
        case 0:
          for (i = 0; i < nSize; i++)
          {
            int temp;
            ReadFile(hFile, &c, 2, &dwCount, NULL);
            sscanf_s(c, "%2x", &temp);
            // Write program memory into 1M map (ignor fuse data)
            nAddr = (nAddrH << 16) | nAddrL;
            if (nAddrH < 0x8000)
              nMap[nAddr] = temp;
            nChecksum += temp;
            nAddrL ++;
          }
          break;
        case 1:
          bEOF = true;
          break;
        case 2:
          printf("Encountered extended segment... exiting\n");
          bOK = false;
          break;
        case 4:
          ReadFile(hFile, &c, 4, &dwCount, NULL);
          sscanf_s(c, "%4x", &nAddrH);
          nChecksum += nAddrH & 0xff;
          nChecksum += (nAddrH >> 8) & 0xff;
          if (nAddrH >= 0x8000)
            printf("ignoring fuse data... ");
          break;
		default:
          bEOF = true;
		  break;
      }
      unsigned int nRxCheck;
      ReadFile(hFile, &c, 2, &dwCount, NULL);
      sscanf_s(c, "%2x", &nRxCheck);
      nChecksum = 256 - nChecksum;
      bOK = (nRxCheck == nChecksum);
      if (!bOK)
        printf("Checksum error\n");
    }
  }
  if (bOK)
    printf("processed %d records\n", nRecord);

  // close hex file
  if (hFile != NULL)
    CloseHandle(hFile);

  // make sure that no code in map overlaps the bootloader space
  // from word addresses 0x000400 to 0x000800 in memory
  // (these are byte address 0x800 to 0xFFF in the byte map)
  if (bOK)
  {
    for (int i = 0x800; i < 0x1000; i++)
	  bOK &= nMap[i] == 0xFF;
    if (!bOK)
      printf("Source file overlaps bootloader from 0x000400 to 0x0007FF... exiting\n");
  }

  // make there is a goto 0x000800 at word addresses 0 and 1
  // goto instruction is: 0000 0100 0000 1000 0000 0000 (0x040800)
  //                      0000 0000 0000 0000 0000 0000 (0x000000)
  // addresses 0-5 contents are: 0x00, 0x08, 0x04, 0x00, 0x00, 0x00
  if (bOK)
  {
    bOK = (nMap[0] == 0) && (nMap[1] == 0x08) && (nMap[2] == 0x04) 
		&& (nMap[4] == 0x00) && (nMap[5] == 0x00) && (nMap[6] == 0x00);
	if (!bOK) 
      printf("Code (__reset) must start at address 0x000800 in memory... exiting\n");
  }
  // change first instruction to goto 0x000400 (address of the bootloader)
  if (bOK)
  {
    nMap[1] = 0x04;
  }
  // prepare to write to PIC on COM port
  if (bOK)
  {
    // close hyperterminal so port can be shared if requested
    if (bCloseHyperterminal)
    {
      HWND hWnd = FindWindow("SESSION_WINDOW", NULL);
      if (hWnd != 0)
      {
        printf("Closing hyperterminal session\n");
        SendMessage(hWnd, WM_COMMAND, 0x191, 0x000);
      }
    }

	// open com port
    printf("Opening %s... ", strPort);
    hPort = CreateFile(strPort, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, NULL, NULL);
    if (hPort == NULL)
    {
      bOK = false;
      printf("could not open port\n");
    }
    // set timeouts
    COMMTIMEOUTS cto;
    cto.ReadIntervalTimeout = 1000;
    cto.ReadTotalTimeoutMultiplier = 1000;
    cto.ReadTotalTimeoutConstant = 0;
    cto.WriteTotalTimeoutConstant = 0;
    cto.WriteTotalTimeoutMultiplier = 1000;
    SetCommTimeouts(hPort, &cto);
    // update device control block for port
    if (bOK)
    {
      FillMemory(&dcb, sizeof(dcb), 0);
      bOK = GetCommState(hPort, &dcb) != 0;
      // 38400 8N1
      dcb.BaudRate = CBR_38400; 
      dcb.ByteSize = 8;
      dcb.Parity = NOPARITY;
      dcb.StopBits = ONESTOPBIT;
      // binary transfers with no handshaking
      dcb.fBinary = TRUE;
      dcb.fOutxCtsFlow = FALSE;
      dcb.fOutxDsrFlow = FALSE;
      dcb.fDsrSensitivity = false;
      dcb.fDtrControl = DTR_CONTROL_ENABLE;
      dcb.fRtsControl = RTS_CONTROL_ENABLE;
      dcb.fOutX = false;
      bOK &= (SetCommState(hPort, &dcb) != 0);
      if (!bOK) printf("error setting dcb\n");
    }
    if (bOK)
      printf("successful\n");
  }

  // find target device
  if (bOK)
  {
    printf("Finding target device..");
    dwCount = 0;
    bOK = false;
    while ((nRetryCount < 30) && !bOK)
    {
      printf(".");
	  // write keyphrase
      WriteFile(hPort, "33F", 3, &dwCount, NULL);
      FlushFileBuffers(hPort);
	  // get acknowledgement (k)
      ReadFile(hPort, &nData, 1, &dwCount, NULL);
      bOK = (dwCount != 0) && (nData == 'k');
      nRetryCount++;
    }
    if (bOK)
      printf(" successful\n");
    else
      printf(" error\n");
  }

  // write header and data pages to PIC
  if (bOK)
  {
    nMaxAddr = 0;
    for (int i = 0; i < 1048576; i++)
    {
      if (nMap[i] != 0xFF)
        nMaxAddr = i;
    }
	// Number of pages is (max address / 2048) + 1, but excluding page 1, page count is nAddr / 2048
	int nPages = (nMaxAddr / 2048);
    printf("Downloading %d program words (%d pages) from 0x000000 to 0x%.6x\n", 
	  (nAddr + 1) / 4, nPages, nAddr / 2);
    printf("  (bootloader page from 0x000400 to 0x0007FF will be skipped)\n"); 
    if (bOK)
    {
      // send 16b block count
      #pragma warning (disable: 4244)
      nData = nPages & 0xff;
      #pragma warning (default: 4244)
      nChecksum = nData;
      WriteFile(hPort, &nData, 1, &dwCount, NULL);
      #pragma warning (disable: 4244)
      nData = (nPages >> 8) & 0xff;
      #pragma warning (default: 4244)
      nChecksum += nData;
      WriteFile(hPort, &nData, 1, &dwCount, NULL);
	  // send header checksum
      WriteFile(hPort, &nChecksum, 1, &dwCount, NULL);
	  // flush out buffers in preparation for receiving checksum back
      FlushFileBuffers(hPort);
	  // read checksum back
      ReadFile(hPort, &nData, 1, &dwCount, NULL);
      if (nData != nChecksum)
      {
        bOK = false;
        printf("Checksum error in header: TX %2x, RX %2x\n", nChecksum, nData);
      }
	  // write pages of 512 program words (1536 bytes)
      nAddr = 0;
	  int nPage = 0;
      while (bOK && (nPage < nPages))
      {
	    // write starting word address for page
        #pragma warning (disable: 4244)
        nData = (nAddr >> 1) & 0xff;
        #pragma warning (default: 4244)
        nChecksum = nData;
        WriteFile(hPort, &nData, 1, &dwCount, NULL);
        #pragma warning (disable: 4244)
        nData = (nAddr >> 9) & 0xff;
        #pragma warning (default: 4244)
        nChecksum += nData;
        WriteFile(hPort, &nData, 1, &dwCount, NULL);
        #pragma warning (disable: 4244)
        nData = (nAddr >> 17) & 0xff;
        #pragma warning (default: 4244)
        nChecksum += nData;
        WriteFile(hPort, &nData, 1, &dwCount, NULL);
        // write only LSW and LSB of MSW to reduce data on serial port        
		for (int i = 0; i < 2048; i++)
        {
	      if ((i & 3) != 3)
 		  {
            nChecksum += nMap[nAddr];
            WriteFile(hPort, &nMap[nAddr], 1, &dwCount, NULL);
		  }
		  nAddr++;
        }
		WriteFile(hPort, &nChecksum, 1, &dwCount, NULL);
        FlushFileBuffers(hPort);
        ReadFile(hPort, &nData, 1, &dwCount, NULL);
        Sleep(20);
        if (nData != nChecksum)
        {
          bOK = false;
          printf("Checksum error at word address 0x%.6x: TX %2x, RX %2x\n", (nAddr - 2048) / 2, nChecksum, nData);
        }
        // skip past page 1 from word address 0x000400 to 0x0007FF
		if (nAddr == 2048) nAddr = 4096;
        // make sure write is done
		ReadFile(hPort, &nData, 1, &dwCount, NULL);
        if (nData != 'd')
        {
          bOK = false;
          printf("Error in write done flag at address %6x\n", (nAddr - 2048) / 2);
        }
        nPage++;
      }
    }
  }

  // close serial port
  if (hPort != NULL)
    CloseHandle(hPort);

  // re-open hyperterminal connected on shared port if requested
  if (bOK)
  {
    if (bOpenHyperterminal)
    {
      HWND hWnd = FindWindow("SESSION_WINDOW", NULL);
      if (hWnd != 0)
      {
        printf("Reopening hyperterminal session\n");
        SendMessage(hWnd, WM_COMMAND, 0x190, 0x000);
      }
    }
    printf("Successful\n");
  }
  printf("\n");

  // exit
  return 0;
}

