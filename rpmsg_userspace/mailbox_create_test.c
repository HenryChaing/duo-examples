#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include "rpmsg.h"
#include "pingpong_common.h"

/* device interface path name */
static char *rpmsg_sys_dev_name;
static char *rpmsg_dev_name;

/* file desripter variable */
static int fd_sys_name,fd_create_ept,fd_ept;

/* rpmsg device attribute : name */
static char buf[20];

/**
  *   Open parameter decided endpoint (fd_ept)
  * 
  *
  */
static int find_local_endpoint(int endpoint){

    int remote_ept; int find = 0; 
    char dst_string[2];

    sprintf(rpmsg_sys_dev_name, "/sys/class/rpmsg/rpmsg%d/dst", 0);
    sprintf(rpmsg_dev_name, "/dev/rpmsg%d", 0);
    sprintf(dst_string, "%d", endpoint);
    
    /* find out the endpoint have the same dst with parameter */
    for (int i = 1; i < 10; i++) {
        fd_sys_name = open(rpmsg_sys_dev_name, O_RDONLY); // backend creates endpoint
        if (fd_sys_name < 0) {
            sprintf(rpmsg_sys_dev_name, "/sys/class/rpmsg/rpmsg%d/dst", i);
            sprintf(rpmsg_dev_name, "/dev/rpmsg%d", i);
            continue;
        }
        
        /* read local endpoint dst */
        read(fd_sys_name, &buf, sizeof(buf));
        char *temp = buf;
        while(temp){
            if(*temp == '\n'){
                *temp = '\0';
                break;
            }
            temp += 1;
        }

        /* compare endpoint dst */
        if (!strcmp(buf, dst_string)){
            fd_ept = open(rpmsg_dev_name, O_RDWR);
            if (fd_ept < 0) {
                printf("no such file or device: %d\n", -ENODEV);
                close(fd_sys_name);
                return -ENODEV;
            }
            printf("open endpoint %s\n",rpmsg_dev_name);
            find = 1;
            break;
        }
        close(fd_sys_name);
        sprintf(rpmsg_sys_dev_name, "/sys/class/rpmsg/rpmsg%d/dst", i);
        sprintf(rpmsg_dev_name, "/dev/rpmsg%d", i);
    }
    close(fd_sys_name);

    if (!find) {
        return -ENODEV;
    }
    return 0;
}

/**
  *   This application create or destroy the remote endpoints
  * 
  *
  */
int main()
{
    int status = 0;
    int ept_cnt = 0; 
    
    /* device interface path name */
    rpmsg_sys_dev_name = malloc(sizeof(char) * 50);
    sprintf(rpmsg_sys_dev_name, "/sys/class/rpmsg/rpmsg%d/name", 0);
    rpmsg_dev_name = malloc(sizeof(char) * 50);
    sprintf(rpmsg_dev_name, "/dev/rpmsg%d", 0);

    /* rpmsg data for create endpoint */
    CONTROL_MESSAGE r5_data = {.CMD = CTR_CMD_CREATE_EP, .ACK_REQUIRED = 0, .DATA = 0};
    /* rpmsg data for normal endpoint */
    THE_MESSAGE pingpong_data = {.DATA = 0};

    /* create endpoint interface */
    struct rpmsg_endpoint_info ept_info = {"create-ept", 0x1c, 0x1c};
    int fd = open("/dev/rpmsg_ctrl0", O_RDWR);
    ioctl(fd, RPMSG_CREATE_EPT_IOCTL, &ept_info);

    /* iteration for finding create endpoint */
    for (int i = 1; i < 10; i++) {
        fd_sys_name = open(rpmsg_sys_dev_name, O_RDONLY);
        if (fd_sys_name < 0) {
            printf("no such file or device: %d\n", -ENODEV);
            return -ENODEV;
        }
        
        status = read(fd_sys_name, &buf, sizeof(buf));
        if (!strncmp(buf, "create-ept", 5)){
            fd_create_ept = open(rpmsg_dev_name, O_RDWR);
            if (fd_create_ept < 0) {
                printf("no such file or device: %d\n", -ENODEV);
                return -ENODEV;
            }
            printf("open endpoint %s\n",rpmsg_dev_name);
            close(fd_sys_name);
            break;
        }
        close(fd_sys_name);
        status = sprintf(rpmsg_sys_dev_name, "/sys/class/rpmsg/rpmsg%d/name", i);
        status = sprintf(rpmsg_dev_name, "/dev/rpmsg%d", i);
    }

    /* receive data from remote device */
    for (;;)
    {    
        printf("input command (0:exit, 1:create endpoint, 2:destroy endpoint) :\n");
        scanf("%d",&r5_data.CMD);
        if (!r5_data.CMD)
            goto end;
        r5_data.CMD += 9;
        if (r5_data.CMD == CTR_CMD_CREATE_EP) {
            CONTROL_MESSAGE_DATA_CREATE_EPT_PARAM data_create_ept_param = {0};
            printf("input create endpoint number :\n");
            scanf("%d",&data_create_ept_param.ept_to_create_addr);
            /* create remote endpoint */
            memcpy(&r5_data.DATA,&data_create_ept_param,sizeof(CONTROL_MESSAGE_DATA_CREATE_EPT_PARAM));
            write(fd_create_ept, &r5_data, sizeof(THE_MESSAGE));
            /* create local endpoint */
            sprintf(rpmsg_dev_name, "pingpong-ept%d", ept_cnt);
            ept_cnt += 1;
            struct rpmsg_endpoint_info ept_info = {"", data_create_ept_param.ept_to_create_addr, data_create_ept_param.ept_to_create_addr};
            memcpy(&ept_info.name,rpmsg_dev_name,sizeof(ept_info.name));
            ioctl(fd, RPMSG_CREATE_EPT_IOCTL, &ept_info);

        } else if (r5_data.CMD == CTR_CMD_DESTROY_EP) {
            CONTROL_MESSAGE_DATA_DESTROY_EPT_PARAM data_destroy_ept_param;
            printf("input destroy endpoint number :\n");
            scanf("%d",&data_destroy_ept_param.ept_to_destroy_addr);
            /* remove remote endpoint */
            memcpy(&r5_data.DATA, &data_destroy_ept_param, sizeof(CONTROL_MESSAGE_DATA_DESTROY_EPT_PARAM));
            write(fd_create_ept, &r5_data, sizeof(THE_MESSAGE));
            /* remove local endpoint */
            {
                status = find_local_endpoint(data_destroy_ept_param.ept_to_destroy_addr); 
                if (status != 0) {
                    printf("can not find local endpoint\n");
                    goto end;
                }

                status = ioctl(fd_ept, RPMSG_DESTROY_EPT_IOCTL);
                if (status) {
                    printf("destroy not success : %d\n", -status);
                } 

                close(fd_ept);
            }  

        } else if (r5_data.CMD == CTR_CMD_SEND) {

            int remote_ept; char dst_string[2];
            printf("input send endpoint number :\n");
            scanf("%d",&remote_ept);

            sprintf(rpmsg_sys_dev_name, "/sys/class/rpmsg/rpmsg%d/dst", 0);
            sprintf(rpmsg_dev_name, "/dev/rpmsg%d", 0);
            sprintf(dst_string, "%d", remote_ept);
            
            /* find local endpoint */
            status = find_local_endpoint(remote_ept);
            if (status != 0) {
                printf("can not find local endpoint\n");
                goto end;
            }

            /* send from local endpoint */
            status = write(fd_ept, &pingpong_data, sizeof(THE_MESSAGE));
            if (status) {
                printf("the write status : %d\n", status);
            }  
            for (int i = 0; i < TC_TRANSFER_COUNT; i++)
            {    
                status = read(fd_ept, &pingpong_data, sizeof(THE_MESSAGE));
                printf("ThreadX_data.DATA: %d\n",pingpong_data.DATA);
                status = write(fd_ept, &pingpong_data, sizeof(THE_MESSAGE));
            }
            close(fd_ept);
        }
    }

end:
    /* destroy create endpoint */
    ioctl(fd_create_ept, RPMSG_DESTROY_EPT_IOCTL);

    /* destroy all local endpoint */
    sprintf(rpmsg_dev_name, "/dev/rpmsg%d", 0);
    for (int i = 1; i < 10; i++) {
        
        fd_ept = open(rpmsg_dev_name, O_RDWR);
        if (fd_ept < 0) {
            status = sprintf(rpmsg_dev_name, "/dev/rpmsg%d", i);
            continue;
        } else {
            ioctl(fd_ept, RPMSG_DESTROY_EPT_IOCTL);        
        }

        status = sprintf(rpmsg_dev_name, "/dev/rpmsg%d", i);
    }

    return 0;
}