#include<stdio.h>
#include<fcntl.h>
#include<string.h>
#include<stdlib.h>
#include<libaio.h>
#include<errno.h>
#include<unistd.h>
#include<sys/time.h>
int main(void){
    int output_fd;
    int buffer_size=1024*1024*1024;
    char *content=(char*)malloc(buffer_size);

    struct timeval start,end;
    for(int i=0;i<buffer_size;++i)
    {
    content[i]='a';
    }
    const char *outputfile="aio.txt";
    io_context_t ctx;
    struct iocb io,*p=&io;
    struct io_event e;
    struct timespec timeout;
    memset(&ctx,0,sizeof(ctx)); 
    if(io_setup(10,&ctx)!=0){//init
        printf("io_setup error\n"); 
        return -1; 
    }   
    if((output_fd=open(outputfile,O_CREAT|O_WRONLY|__O_DIRECT,0644))<0){   
        perror("open error");
        io_destroy(ctx);
        return -1; 
    } 
    io.data=content;
    gettimeofday(&start,NULL);
    io_prep_pwrite(&io,output_fd,content,buffer_size,0);
    
    if(io_submit(ctx,1,&p)!=1){
        
        io_destroy(ctx);
        printf("io_submit error\n");    
        return -1; 
    }   
    gettimeofday(&end,NULL);
    while(1){
        timeout.tv_sec=0;
        timeout.tv_nsec=500000000;//0.5s
        if(io_getevents(ctx,0,1,&e,&timeout)==1){   
            close(output_fd);
            break;
        }   
        printf("haven't done\n");
        sleep(1);
    }   
    io_destroy(ctx);

    printf("%f\n",(end.tv_sec-start.tv_sec)*1000000.0+(end.tv_usec-start.tv_usec));
    return 0;
}