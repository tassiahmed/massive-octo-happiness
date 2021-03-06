/* server.c */
//Names: Evan Thompson, John Cusick, Tausif Ahmed   


#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>

#include <netinet/in.h>
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <cstdlib>
#include <arpa/inet.h>
#include <pthread.h>
#include "pagingSys.h"
#include "fileSys.h"


#define BUFFER_SIZE 1025

#define PORT_NUM 8765
#define NUM_FRAMES 32
#define SIZE_FRAMES 1024
#define FRAMES_PER_FILE 4

typedef unsigned char BYTE;

//global variable                                                                                   


#define MAX_ARG 100


//passed to each client thread
struct clientArg{
  struct sockaddr_in * client;
  int *newsock; 
};

//used to parse a query
struct queryType{
  char ** argv;
  int argc;
  int type;
  /*
    types:
    1: STORE
    2: READ
    3: DELETE
    4: DIR
    5: STORE's file-contents
   */
};






//==============global variables=================

//global thread counter (for tid[])
//may encounter some critical sections here -- be wary
int tCounter = 0;

/* global mutex variable */
pthread_mutex_t addMutex = PTHREAD_MUTEX_INITIALIZER;  //for new threads

/* global mutex variable */
pthread_mutex_t storeMutex = PTHREAD_MUTEX_INITIALIZER;  //for new clients


//holds all Thread IDs
pthread_t * tids; 

//the paging
PagingSystem* paging;

int sock;

//file system
FileSystem* filesys;

//================================================================
//========================FUNCTIONS===============================
//================================================================




//send command to send message
void sendClient(int socket, const char * str, int len){
  int numSent = send( socket, str, len, 0 );
  fflush( NULL );
  if ( numSent != len ){
    perror( "send() failed" );
  }
}


//send command to send binary bytes
void sendContents(int socket, const char * str, int len){
  int numSent = write( socket, str, len);
  fflush( NULL );
  if ( numSent != len ){
    perror( "send() failed" );
  }
}


//number of digits
int numDig(unsigned int x){
  int length = 1;
  while ( x /= 10 ){
    length++;
  }
  return length;
}


/*
  Parse server queries into argv/argc data type
  Return Value: 0 = success, 1 = unknown command
*/

//could add test to make sure don't read in MAX_ARG arguments, but unneccesary
int parseQuery(char *comm, struct queryType * query){
  query->argc = 0;  

  //tokenize by spaces
  char * tmp = strtok(comm, " ");
  int counter = 0;
  
  //look at first token for a command
  if(tmp == NULL){
    printf("TEMP IS NULL\n");
    return 1; //should not be the case but for safety
  }

  if(strcmp(tmp,"\n")==0){ // no input --ignore 
    return -1;
  }

  //the \n was also a part of it (which is fine, because should be <comm> w/ a space
  if(strcmp(tmp,"STORE") == 0){
    query->type = 1;
  }
  else if(strcmp(tmp,"READ") == 0){
    query->type = 2;
  }
  else if(strcmp(tmp,"DELETE") == 0){
    query->type = 3;
  }
  else if(strcmp(tmp,"DIR\n") == 0){
    query->type = 4;
  }
  else{
    //must be a packet with file-info!!!!!!!!
    query->type = 5;
    //first fix the buffer
    if(comm[strlen(tmp)] != '\0'){ //idk to be safe?
      comm[strlen(tmp)] = ' ';
    }
    return 1;
  }

  //set argv data type for correct commands
  while(tmp!=NULL){
    query->argv[counter] = tmp;
    query->argc++;
    tmp = strtok(NULL, " ");
    counter++;
  }


  //std::cout<<"argc: "<<query->argc<<std::endl;
  
  return 0; //success
}



//================================================================================
//===============================CLIENT THREAD====================================
//================================================================================


void * client_thread(void * arg){
  struct clientArg * clientInfo = (struct clientArg *) arg;
  int n;
  char * buffer = new char[BUFFER_SIZE+1];
  unsigned long tid = (unsigned long) pthread_self();
  bool command = 1;  //default to a command
  std::vector<std::string> file_names;

  //int numRecv;
  int numBytes = 0;
  char * bigBuff;
  char *tmpBig;
  int currBytes = 0;
  char *fName;

  std::vector<BYTE> storeInput;
	

  struct queryType * query = new struct queryType; 
  query->argv = new char *[MAX_ARG];
  
  //keep recving while the client is up
  do{
    
    if(command == 1){  //if expecting a command
      n = recv( *(clientInfo->newsock), buffer, BUFFER_SIZE, 0 );
    }
    else{  //if expecting file contents (after STORE)  use read for byte
      n = read( *(clientInfo->newsock), tmpBig, numBytes );
    }
    
    if ( n < 0 ){
      perror( "recv() failed" );
    }
    else if ( n == 0 ){
      //socket closed!
    }
    else{
      
      //--------------------------------------------SOME TYPE OF DATA RECEIVED----------------------------------------------

      int rc = 1;
      if(command == 1){  //parse the command
	buffer[n] = '\0';  /* assuming text.... */
	rc = parseQuery(buffer,query);
      }

      if(rc == -1){  //no input, do no acknowledge
	continue;
      }

      //----------------------------NOT A KNOWN COMMAND-----------------------------------
      if(rc == 1 && command == 1){
	//no command and expecting command
	char * comm = strtok(buffer, " ");
	if(comm[strlen(comm)-1] == '\n'){
	  comm[strlen(comm)-1] ='\0';
	}
	printf( "[thread %lu] Rcvd: %s\n", tid, comm);
	printf( "[thread %lu] Sent: ERROR: NO SUCH COMMAND\n", tid );
	int sendNum = send( *(clientInfo->newsock), "ERROR: NO SUCH COMMAND\n", 23, 0 );
	fflush( NULL );
	if ( sendNum != 23){
	  perror( "send() failed" );
	}
      }


      
      //================================================================================
      //===============================FILE-CONTENTS====================================
      //================================================================================


      if(rc == 1 && command == 0){  //2nd part of store
	//was expecting file contents

	  
	for(int i = 0; i < n; i++){  //n is number of bytes read
	  storeInput.push_back((BYTE)tmpBig[i]);
	}

	
	//debug print
	/*
	for(unsigned int i = 0; i < storeInput.size(); i++){
	  printf("storeInput[%u]: %c\n",i,(char)storeInput[i]);
	}
	*/

	/*
	if(currBytes == 0){
	  strncpy(bigBuff, tmpBig, numBytes);
	}
	else{
	  strncat(bigBuff, tmpBig,numBytes-currBytes);
	}
	*/

	currBytes+=n;  //n set by recv, numBytes received
		
	if(currBytes == numBytes){  //ready to store, go ahead and lock
	  pthread_mutex_lock( &storeMutex );    /*   P(mutex)  */
	  //CRITICAL SECTION: storing
	  
	  
	  /*
	    rc: -1 if it already exists
	    total # written if it doesn't
	  */
	  int rc = paging->store(fName, storeInput);       
	  filesys->addFile(fName);

	  //END CRITICAL SECTION                                                                          
	  pthread_mutex_unlock( &storeMutex );  /*   V(mutex)  */
	  
	  free(bigBuff);
	  free(tmpBig);
	  free(fName);
	  
	  command = 1;
	  
	  if(rc == -1){  //file already existed,  SORRY CHAP
	    printf( "[thread %lu] Sent: ERROR: FILE EXISTS\n",tid);
	    sendClient(*(clientInfo->newsock),"ERROR: FILE EXISTS\n",19);
	  }
	  else{  //was able to store the file!
	    printf( "[thread %lu] Transferred file (%d bytes)\n",tid, numBytes);
	    printf( "[thread %lu] Sent: ACK\n",tid);
	    sendClient(*(clientInfo->newsock),"ACK\n", 4);
	  }
	  

	  currBytes = 0;
	  storeInput.clear();

	}
      }
      if(rc == 0){ //got a command
	
	//================================================================================
	//===============================STORE============================================
	//================================================================================	
       
	if(query->type == 1){  //store
	
	  if(query->argc != 3){
	    printf( "[thread %lu] Rcvd: STORE with %d arguments\n",tid, query->argc);
	    printf( "[thread %lu] Sent: ERROR: INCORRECT ARGUMENTS\n",tid);
	    sendClient(*(clientInfo->newsock),"ERROR: INCORRECT ARGUMENTS\n",27);
	    continue;
	  }
  
	  numBytes = atoi(query->argv[2]);
	  command = 0;
	  bigBuff = (char *) calloc(numBytes, sizeof(char));
	  tmpBig = (char *) calloc(numBytes, sizeof(char));
	  
	  fName = (char *) calloc(strlen(query->argv[1]), sizeof(char));
	  strcpy(fName, query->argv[1]);
	  
	  printf( "[thread %lu] Rcvd: STORE %s %d\n",tid, fName, numBytes);
	  

	}


	//================================================================================
	//===============================READ=============================================
	//================================================================================


	else if(query->type == 2){  //read
	  //pthread_mutex_lock( &RDMutex );    /*   P(mutex)  */
	  //CRITICAL SECTION: read & delete

	  if(query->argc != 4){
	    printf( "[thread %lu] Rcvd: READ with %d arguments\n",tid, query->argc);
	    printf( "[thread %lu] Sent: ERROR: INCORRECT ARGUMENTS\n",tid);
	    sendClient(*(clientInfo->newsock),"ERROR: INCORRECT ARGUMENTS\n",27);
	    continue;
	  }


	  int offset = atoi(query->argv[2]);
	  int numRead = atoi(query->argv[3]);
	  
	  int *flag = new int; 
	  /*
	    0:success
	    1:file DNE
	    2:byte range invalid
	    ...
	   */

	  int currRead = 0;
	  int stopVal = 0;

	  int startVal = 0;
	  
	


	  printf( "[thread %lu] Rcvd: READ %s %d %d\n", tid , query->argv[1] , offset, numRead);
	      


	  //if file queued for deletion
	  File * fileP;
	  int fileIndex = filesys->findFile(query->argv[1]);
	  //printf("Done with find\n");
	  if(fileIndex != -1){  //file exists
	    
	    fileP = filesys->files[fileIndex];
	    if(fileP->readAble == 0){  //can't read, queued for deletion
	      sendClient(*(clientInfo->newsock),"ERROR: FILE QUEUED FOR DELETION\n",32);
	      printf( "[thread %lu] Sent: ERROR: FILE QUEUED FOR DELETION\n", tid );
	      delete flag;
	      continue;
	    }
	    fileP->numReads++;  //can read
	    
	  }

	  

	  //use a counter here if you need to do +1 or something (maybe no counter maybe just if(currRead==0) dont add 1, else +1 -------- for startVal
	  while(currRead != numRead){
	    std::vector<BYTE> tmpVec;
	    
	    startVal = offset+currRead;
	    if((startVal%SIZE_FRAMES)+(numRead - currRead) > SIZE_FRAMES){  //go to end of frame
	      stopVal = SIZE_FRAMES;  //end of frame
	    }
	    else{  //this is our last frame
	      stopVal = numRead-currRead;
	    }
	    
	    //printf("START VAL: %d\n",startVal);
	    //printf("STOP VAL: %d\n",stopVal);

	    tmpVec = paging->read_page(query->argv[1], startVal, stopVal, flag);
	    
	    if(*flag == 1){
	      sendClient(*(clientInfo->newsock),"ERROR: NO SUCH FILE\n",20);
	      printf( "[thread %lu] Sent: ERROR: NO SUCH FILE\n", tid);
	      break;
	    }
	    
	    else if(*flag == 2){
	      sendClient(*(clientInfo->newsock),"ERROR: INVALID BYTE RANGE\n",26);
	      printf( "[thread %lu] Sent: ERROR: INVALID BYTE RANGE\n", tid);
	      break;
	    }
	    
	    /*
	    for(unsigned int i = 0; i < tmpVec.size(); i++){
	      output.push_back(tmpVec[i]);
	    } 
	    */
	    currRead+=tmpVec.size();

	   
	    
	    //printf("SIZE OF OUTPUT: %lu\n", output.size());
	    //printf("numREAD: %d\n", numRead);
	    
	    char * clientOut = (char * )calloc(tmpVec.size(), sizeof(char));
	      


	    for(unsigned int i = 0; i  < tmpVec.size(); i++){
	      clientOut[i] = (char) tmpVec[i];
	      //std::cout<<"output["<<i<<"] = "<<output[i]<<std::endl;
	    }
	    
	    
	    
	    //convert number to char*
	    std::stringstream strs;
	    strs << tmpVec.size();
	    std::string temp_str = strs.str();
	    const char* numOut = (char*) temp_str.c_str();
	    
	    //int numDigits = numDig(file_names.size());  //find number of digits
	    
	    
	    char firstOut[strlen(numOut) + 5];  //5: ack \n
	    
	    strcpy(firstOut,"ACK ");
	    strcat(firstOut, numOut);
	    strcat(firstOut,"\n");
	      
	    //ACK
	    sendClient( *(clientInfo->newsock), firstOut, strlen(firstOut));
	    printf( "[thread %lu] Sent: ACK %lu\n", tid , tmpVec.size());
	  
	      
	    //file contents
	    sendContents( *(clientInfo->newsock), clientOut, tmpVec.size());
	    printf( "[thread %lu] Transferred %lu Bytes from offset %d\n", tid , tmpVec.size(), startVal);
	      
	    
	    
	    
	    free(clientOut);
	    
	  }
	  
	  if(fileIndex!=-1){
	    fileP->numReads--;
	  }	    
	  

	  delete flag;
	  //END CRITICAL SECTION                                                                          
	  //pthread_mutex_unlock( &RDMutex );  /*   V(mutex)  */

	}

	//===========================================================
	//=========================DELETE============================
	//===========================================================

	else if(query->type == 3){  //delete command, some synchronization required


	  if(query->argc != 2){
	    printf( "[thread %lu] Rcvd: DELETE with %d arguments\n",tid, query->argc);
	    printf( "[thread %lu] Sent: ERROR: INCORRECT ARGUMENTS\n",tid);
	    sendClient(*(clientInfo->newsock),"ERROR: INCORRECT ARGUMENTS\n",27);
	    continue;
	  }


	  //printf("start delete\n");
	  
	  //filesys->print();
	  

	  //printf("end debug print\n");
	  query->argv[1][strlen(query->argv[1])-1] = '\0';  //get rid of \n
	  //printf("delete2\n");
	  //printf("argv[1]: %s\n",query->argv[1]);
	  printf( "[thread %lu] Rcvd: DELETE %s\n", tid , query->argv[1]);

	  int rc = filesys->removeFile(query->argv[1]);
	  //printf("delete3\n");
	  if(rc == 1){
	    //printf("FILE NOT FOUND: %s\n",query->argv[1]);
	    sendClient( *(clientInfo->newsock), "ERROR: NO SUCH FILE\n",20);
	    printf( "[thread %lu] Sent: ERROR: NO SUCH FILE\n", tid);
	  }
	  else{  //found file
	    
	    //set file to be deleted

	    //wait for reads to finish
	    
	    paging->delete_file(std::string(query->argv[1]));
	    printf( "[thread %lu] Deleted %s file\n", tid , query->argv[1]);
	    sendClient( *(clientInfo->newsock), "ACK\n",4);
	    printf( "[thread %lu] Sent: ACK\n", tid);
	    
	  }
	}



	//================================================================================
	//=========================================DIR====================================
	//================================================================================



	else if(query->type == 4){  //dir command, no synchronization required

	  //CHECKED IN PARSE
	  /*
	  if(query->argc != 3){
	    printf( "[thread %lu] Rcvd: DIR with %d commands\n",tid, query->argc);
	    printf( "[thread %lu] Sent: ERROR: INCORRECT ARGUMENTS\n",tid);
	    sendClient(*(clientInfo->newsock),"ERROR: INCORRECT ARGUMENTS\n",27);
	    continue;
	  }
	  */

	  file_names = paging->dir();
	  //no need for 0 files check, this line is generalized
	  //Note: no files just returns 0\n

	  printf( "[thread %lu] Rcvd: DIR\n", tid);

	  if(file_names.size()==0){
	    n = send( *(clientInfo->newsock), "0\n", 2, 0 );
	    fflush( NULL );
	    if ( n != 2){
	      perror( "send() failed" );
	    }

	  }
	  else{
	    //convert number to char*
	    std::stringstream strs;
	    strs << file_names.size();
	    std::string temp_str = strs.str();
	    const char* numFiles = (char*) temp_str.c_str();


	    
	    
	    int numDigits = numDig(file_names.size());  //find number of digits
	    
	    //std::cout<<"numFiles: "<<numFiles<<std::endl;
	    //std::cout<<"numDigits: "<<numDigits<<std::endl;
	    
	    char outTmp[numDigits+1];
	    strcpy(outTmp, numFiles);
	    outTmp[numDigits] = '\n';
	    
	    n = send( *(clientInfo->newsock), outTmp, numDigits + 1, 0 );
	    fflush( NULL );
	    if ( n != numDigits + 1){
	      perror( "send() failed" );
	    }
	    
	    for(unsigned int i = 0; i < file_names.size(); i++){
	      //std::cout<<file_names[i]<<std::endl;
	      std::string str = file_names[i];
	      const char* charStr = (char*) str.c_str();
	      char out[strlen(charStr)+1];
	      strcpy(out, charStr);
	      out[strlen(charStr)]='\n';
	      
	      n = send( *(clientInfo->newsock), out, strlen(charStr) + 1, 0 );
	      fflush( NULL );
	      if ( (unsigned int)n != strlen(charStr) + 1){
		perror( "send() failed" );
	      }




	    }
	  }
	  //std::cout<<"Done with DIR"<<std::endl;
	  

	  printf( "[thread %lu] Sent: ACK & %lu file names\n",tid , file_names.size());
	  
	
	}


	
      }
      
      
    }
  }while ( n > 0 );
  /* this do..while loop exits when the recv() call
     returns 0, indicating the remote/client side has
     closed its socket */
  
  printf( "[thread %lu] Client closed its socket....terminating\n", tid );
  close( *(clientInfo->newsock) );
  
  delete [] buffer;
  
  delete [] query->argv;
  delete query;

  delete clientInfo->newsock;
  delete clientInfo->client;
  delete clientInfo;
  
  pthread_exit(NULL); /* thread terminates here! */
}





//================================================================
//=============================MAIN===============================
//================================================================






int main()
{
  
  //paging system init                                                                              
  paging = new PagingSystem(NUM_FRAMES, SIZE_FRAMES, FRAMES_PER_FILE);


  //keep track of pids
  
  //thread ids and bools for if pthread_join has been called on them
  tids = new pthread_t[1000]; 
  
  //  printf("crash is in FileSystem\n");
  
  filesys = new FileSystem;  //must be after init_server (init adds files to server)

 
  //printf("testa\n");

  /* Create the listener socket as TCP (SOCK_STREAM) socket */
  sock = socket( PF_INET, SOCK_STREAM, 0 );

  if ( sock < 0 ){
    perror( "socket() failed" );
    exit( EXIT_FAILURE );
  }
  
  /* socket structures */
  struct sockaddr_in server;

  server.sin_family = PF_INET;
  server.sin_addr.s_addr = INADDR_ANY;

  unsigned short port = 8765;

  /* htons() is host-to-network-short for marshalling */
  /* Internet is "big endian"; Intel is "little endian" */
  server.sin_port = htons( port );
  int len = sizeof( server );

  if ( bind( sock, (struct sockaddr *)&server, len ) < 0 ){
    perror( "bind() failed" );
    exit( EXIT_FAILURE );
  }
  
  listen( sock, 5 );   /* 5 is the max number of waiting clients */
  printf( "Listening on port %d\n", port );

  
  //int pid;
  int rc;
  //char buffer[ BUFFER_SIZE ];



  //printf("testb\n");




  while ( 1 ){
    struct sockaddr_in * client = new struct sockaddr_in;
    int fromlen = sizeof( client );
    
    
    
    //    printf( "PARENT: Blocked on accept()\n" );
    int *newsock = new int; 
    *newsock = accept( sock, (struct sockaddr *)client,
		       (socklen_t*)&fromlen );
    printf( "Received incoming connection from %d.%d.%d.%d\n",
	    int(client->sin_addr.s_addr&0xFF),
	    int((client->sin_addr.s_addr&0xFF00)>>8),
	    int((client->sin_addr.s_addr&0xFF0000)>>16),
	    int((client->sin_addr.s_addr&0xFF000000)>>24));
    
    //set up argument
    struct clientArg * clientInfo = new clientArg;
    clientInfo->client = client;
    clientInfo->newsock = newsock;
    
    /* handle socket in child process */
    
    //CRITICAL SECTION: global tCounter.. dont want to overright any tids  
    pthread_mutex_lock( &addMutex );    /*   P(mutex)  */
    
    rc = pthread_create( &tids[tCounter], NULL, client_thread, (void*)clientInfo);
    if ( rc != 0 ){
      fprintf( stderr, "pthread_create() failed (%d)\n", rc );
    }
    tCounter++;
    
    //END CRITICAL SECTION
    pthread_mutex_unlock( &addMutex );  /*   V(mutex)  */
    
  }
  
  close( sock );

  delete filesys;

  delete paging;
  
  delete [] tids;
  
  return EXIT_SUCCESS;
}
