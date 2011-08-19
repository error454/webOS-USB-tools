#include <iostream>
#include <sstream>
#include <fstream>
#include <sys/statvfs.h>
#include <syslog.h>
using namespace std;

#include "SDL.h"
#include "PDL.h"

#define CHUNK_SIZE 1280	//5 MB chunks at a 4k block size
#define BLOCK_SIZE 4096 //4k

string freeSpacePayload;
string totalSpaceBytes;
string percent;
int currentBlock;
int lastBlock;

char buffer[4096];

FILE *newImage;

const char *freeSpace[2];
const char *percentComplete[1];

//Whether the freespace function has data to be read
bool gotFreeSpace = false;

//Whether the file has been created
bool fileCreationFinished = false;

//Whether file creation has began
bool fileCreationStarted = false;

//Used so the user can cancel file creation
bool cancelFileCreation = false;

//percent complete for creation of the file
float creationPercent = 0.0f;

PDL_bool cancelCreation(PDL_JSParameters *params){
	cancelFileCreation = true;
}

PDL_bool mountImage(PDL_JSParameters *params){
	const char *path = PDL_GetJSParamString(params, 0);
	
	//TODO stat to make sure path exists
	//FILE *mount = fopen(dir.c_str(), "w");
	FILE *mount = fopen("/sys/devices/platform/musb_hdrc.0/gadget/gadget-lun0/file", "w");
	//FILE *mount = fopen("/media/internal/file", "w");
	if(mount != NULL){
		fwrite(path, 1, strlen(path), mount);
		fclose(mount);
	}
	else{
		openlog("USBMASS", LOG_PID|LOG_CONS, LOG_USER);
		syslog(LOG_INFO, "Failed to open file\n");
		closelog();
	}
}

/*
 	Get the free space for the location specified
	
	Input parameters:
		0: The path to stat
		
  	The data returned is:
  		Index 0: Free space in bytes
  		Index 1: Total space in bytes
 */
PDL_bool getFreeSpace(PDL_JSParameters *params){
	const char *location = PDL_GetJSParamString(params, 0);
	
	struct statvfs fiData;
	char fnPath[128];
	
	strcpy(fnPath, location);
	
	if((statvfs(fnPath, &fiData)) < 0){
		freeSpacePayload = "-1";
	}
	else{
		ostringstream ostr;
		ostringstream ostr2;
		
		/*
		double i64 = (double)fiData.f_bsize * fiData.f_bfree;
		openlog("USBMASS", LOG_PID|LOG_CONS, LOG_USER);
		syslog(LOG_INFO, "Location: %s\n", fnPath);
		syslog(LOG_INFO, "block size: %d\n", fiData.f_bsize);
		syslog(LOG_INFO, "blocks free: %d\n", fiData.f_bfree);
		syslog(LOG_INFO, "blocks free non-root: %d\n", fiData.f_bavail);
		syslog(LOG_INFO, "blocks * free: %f\n", i64);
		syslog(LOG_INFO, "free space: %f\n", ((fiData.f_bsize * fiData.f_bfree) / 1024.0) / 1024.0);
		syslog(LOG_INFO, "total space: %f\n", ((fiData.f_bsize * fiData.f_blocks) / 1024.0) / 1024.0);
		closelog();
		*/
		ostr << (double) fiData.f_bsize * fiData.f_bfree / 1024.0 / 1024.0;
		ostr2 << (double) fiData.f_bsize * fiData.f_blocks / 1024.0 / 1024.0;
		
		freeSpacePayload = ostr.str();
		totalSpaceBytes = ostr2.str();
	}
	
	freeSpace[0] = freeSpacePayload.c_str();
	freeSpace[1] = totalSpaceBytes.c_str();
		
	gotFreeSpace = true;
}

/*
	This function creates a data image.
	Input parameters:
		0: The path to create the image in
		1: The size of the image to create
		2: The filename to use
*/
PDL_bool createImage(PDL_JSParameters *params){
	
	//Size in MB * k * b to get total bytes
	int size = PDL_GetJSParamInt(params, 1) * 1024 * 1024;
	
	//Build string for location + filename
	const char *location = PDL_GetJSParamString(params, 0);
	const char *filename = PDL_GetJSParamString(params, 2);
	string dir = location;
	dir.append(filename);
	
	//Set block variables based on size of file
	currentBlock = 0;
	lastBlock = size / BLOCK_SIZE;
	creationPercent = 0.0f;
	
	//Initialize buffer, although it really doesn't matter
	for(int i = 0; i < 4096; i++)
		buffer[i] = '0';
	
	//Attempt to create the file
	newImage = fopen(dir.c_str(), "w");
	if(newImage != NULL){
		fileCreationStarted = true;
		cancelFileCreation = false;
	}
	else
		fileCreationFinished = true;
}

/*
	Continues the file creation process
	 picking up where the last write stopped
	 writing 3 MB and then yielding for a status
	 update.
*/
void continueFileCreation(){
	int j;
	if((lastBlock - currentBlock) >= CHUNK_SIZE)
		j = CHUNK_SIZE + currentBlock;
	else
		j = lastBlock;
		
	for(; currentBlock < j; currentBlock++){		
		//write a 4k block
		fwrite(buffer, 4096, 1, newImage);
	}
	
	if(currentBlock >= lastBlock){
		fileCreationStarted = false;
		fileCreationFinished = true;
	}
}

int main(int argc, char** argv)
{
    // Initialize the SDL library with the Video subsystem
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);

    // start the PDL library
    PDL_Init(0);
 
	PDL_RegisterJSHandler("getFreeSpace", getFreeSpace);
	PDL_RegisterJSHandler("createImage", createImage);
	PDL_RegisterJSHandler("cancelCreation", cancelCreation);
	PDL_RegisterJSHandler("mountImage", mountImage);
	
	PDL_JSRegistrationComplete();
	
    // Event descriptor
    SDL_Event Event;

    do {
    
		if(gotFreeSpace == true){
			PDL_CallJS("getFreeSpaceReturn", freeSpace, 2);
			gotFreeSpace = false;
		}
			
		//If the file creation job has started
		if(fileCreationStarted){
			if(cancelFileCreation){
				fileCreationFinished = true;
				fileCreationStarted = false;
			}
			else{
				//Continue with file creation
				continueFileCreation();
			
				//Calculate percent created
				ostringstream operc;
				creationPercent = (float) currentBlock / lastBlock;
				operc << creationPercent;
				percent = operc.str();
				percentComplete[0] = percent.c_str();
				
				//Return the status of the creation
				PDL_CallJS("getCreationStatus", percentComplete, 1);
			}
		}
		
		//If the file creation job has finished
		if(fileCreationFinished){
			//Close the file if necessary
			if(newImage != NULL)
				fclose(newImage);
				
			fileCreationFinished = false;
			fileCreationStarted = false;
			PDL_CallJS("getCreationFinished", NULL, 0);
		}
		
        // Process the events
        while (SDL_PollEvent(&Event)) {
            switch (Event.type) {
    
                default:
                    break;
            }
        }

		if(!fileCreationStarted)
			SDL_Delay(200);
    } while (Event.type != SDL_QUIT);
    
    // Cleanup
    PDL_Quit();
    SDL_Quit();

    return 0;
}

