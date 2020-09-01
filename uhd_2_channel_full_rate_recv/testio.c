#include<stdio.h>
#include<fcntl.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/time.h>
#include<string.h>


int main()

    
{
    int fd, ret,buffer_size=4096;
    int times=1024*1024;
    char*buffer=(char*)malloc(buffer_size);

    //open file;
    fd=open("test.txt",O_CREAT|O_WRONLY|O_TRUNC,0666);

    if(fd<0)
    {
        perror("open\n");
        exit(-1);
    }
    memset(buffer,'a',buffer_size);
    //write buffer; and calculate time;
    struct timeval start,end;
    gettimeofday(&start,NULL);
    printf("start:%ld,%ld\n",start.tv_sec,start.tv_usec);
    for(int i=0;i<times;++i)
    {
        ret=write(fd,(void*)buffer,buffer_size);
    }

    gettimeofday(&end,NULL);
    printf("end:%ld,%ld\n",end.tv_sec,end.tv_usec);
    if(ret<0)
    {
        perror("write\n");
        exit(-1);
    }
    printf("duration:%f\n",(end.tv_sec-start.tv_sec)*1.0+(end.tv_usec-start.tv_usec)/1000000.0);
    //close file;
    close(fd);
    return 0;
}