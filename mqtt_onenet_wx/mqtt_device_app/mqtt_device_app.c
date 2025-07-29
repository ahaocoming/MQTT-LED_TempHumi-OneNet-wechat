/*******************************************************************************
 * Copyright (c) 2012, 2023 IBM Corp., Ian Craggs
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *   https://www.eclipse.org/legal/epl-2.0/
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial contribution
 *******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"
#include "rpc_client.h"
#include "cfg.h"
#include "cJSON.h"

#include "unistd.h" //sleep()函数

#define ADDRESS     "tcp://192.168.5.10:1883"
#define CLIENTID    "0m1g1fus"
#define TOPIC_SUBSCRIBE     "attributes/push"
#define TOPIC_PUBLISH       "attributes"
#define QOS         0
#define TIMEOUT     10000L

#define USER_NAME "100ask"
#define PASSWORD  "100ask"

int led_state;
char led_state_string[10];
int wx_set_flag = 0;
char ID_string[10];
char set_reply_topic[500];

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    printf("Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    printf("Message arrived\n");
    printf("     topic: %s\n", topicName);
    printf("   message: %.*s\n", message->payloadlen, (char*)message->payload);

/* 根据接收到的消息控制LED */
/* 消息格式: {"LED":0} */
    cJSON *root = cJSON_Parse((char*)message->payload);

    cJSON *ID =  cJSON_GetObjectItem(root, "id");
   
    if(ID){
        printf("ID:%s\n",ID->valuestring);
        if(strcmp(ID->valuestring, "123") == 0){//ID != 123
            wx_set_flag = 0;
        }else{
            wx_set_flag = 1;
            strcpy(ID_string,ID->valuestring);
        }
    }
    cJSON *params = cJSON_GetObjectItem(root, "params");    
    if (params){
         cJSON *LED0 = cJSON_GetObjectItem(params, "LED0");
        if(LED0)
        {
            //printf("LED0:%s\n", cJSON_IsTrue(LED0) ? "true" : "false");
            if(cJSON_IsTrue(LED0)){
                printf("LED0 true\n");
                led_state = rpc_led_control(1);
            }else{
                printf("LED0 false\n");
                led_state = rpc_led_control(0);
            }

        printf("led_state:%d",led_state);      
        }
    }

    cJSON_Delete(root);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause)
{
    printf("\nConnection lost\n");
    if (cause)
    	printf("     cause: %s\n", cause);
}

int main(int argc, char* argv[])
{
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    char URI[500];
    char clientid[500];
    char username[500];
    char password[500];
    char pubtopic[500];
    char subtopic[500];

    char address[500];

    char pub_topic[500];//$thing/up/property/{ProductID}/{DeviceName}
    char sub_topic[500];//$thing/down/property/{ProductID}/{DeviceName}
    
   /* 读配置文件 /etc/ThingsCloud.cfg 得到URI、clientID、username、passWD、ProductID、DeviceName*/
    if( 0 != read_cfg(URI, clientid, username, password, pubtopic, subtopic) )
    {
        printf("read cfg err\n");
        return -1;
    }
    sprintf(address,"%s", URI);

    sprintf(pub_topic,"%s", pubtopic);
    sprintf(sub_topic,"%s", subtopic);
    strcpy(set_reply_topic, "$sys/4urAm6OyLj/LED_TempHumi/thing/property/set_reply");

   /* INIT RPC: connect RPC Server */
    if(RPC_Client_Init() == -1)
    {
        printf("RPC_Client_Init err\n");
        return -1;
    }
   
    if ((rc = MQTTClient_create(&client, address, clientid,
        MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to create client, return code %d\n", rc);
        rc = EXIT_FAILURE;
        goto exit;
    }

    if ((rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to set callbacks, return code %d\n", rc);
        rc = EXIT_FAILURE;
        goto destroy_exit;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = username;
    conn_opts.password = password;
    while(1)
    {
        if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
        {
            printf("Failed to connect, return code %d\n", rc);
            rc = EXIT_FAILURE;
            //goto destroy_exit;
        }
        else
        {
            break;
        }
    }

    printf("Subscribing to topic %s\nfor client %s using QoS%d\n\n"
           "Press Q<Enter> to quit\n\n", TOPIC_SUBSCRIBE, CLIENTID, QOS);
    if ((rc = MQTTClient_subscribe(client, sub_topic, QOS)) != MQTTCLIENT_SUCCESS)
    {
        printf("%s", pubtopic);
        printf("%s", subtopic);
    	printf("Failed to subscribe, return code %d\n", rc);
    	rc = EXIT_FAILURE;
    }
    else
    {
    	int ch;
        int cnt = 0;
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        char buf[1000];
        MQTTClient_deliveryToken token;
        
    	while (1)
    	{
            /* rpc_dht11_read */
            
            char humi, temp;
            while (rpc_dht11_read(&humi, &temp) != 0);
            led_state = rpc_led_read();
            
            sprintf(buf, "{ \
                            \"temp_value\":%d,\
                            \"humi_value\":%d,\
                            \"LED\":%d\
                          }", temp, humi,!led_state);
            
            if(led_state){
                strcpy(led_state_string,"false");
            }else{
                strcpy(led_state_string,"true");
            }
            if(wx_set_flag == 0){
                sprintf(buf, "{\"id\":\"123\",\
                                \"params\":{ \"temp\":{\
                                                \"value\":%d\
                                                },\
                                            \"humi\":{\
                                                \"value\":%d\
                                            },\
                                            \"LED0\":{\
                                                \"value\":%s\
                                            }\
                                            }\
                                }",temp, humi,led_state_string);
            }else{
                sprintf(buf,"{\"id\":\"%s\",\"code\":%d,\"msg\":\"success\"}",ID_string,200);
                printf("buf:%s\n", buf);
            }
                     
            pubmsg.payload = buf;
            pubmsg.payloadlen = (int)strlen(buf);
            pubmsg.qos = QOS;
            pubmsg.retained = 0;

            /* 发布消息 */
            
            if(wx_set_flag == 0){
                
                if ((rc = MQTTClient_publishMessage(client, pub_topic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS)
                {
                    printf("Failed to publish message, return code %d\n", rc);
                    continue;
                }
            }else{    

                if ((rc = MQTTClient_publishMessage(client, set_reply_topic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS)
                {
                    printf("Failed to publish message, return code %d\n", rc);
                    continue;
                }
                printf("buf:%s\n", buf);
                wx_set_flag = 0;
            }

            
            rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
            //printf("Message with delivery token %d delivered\n", token);

            sleep(3);

    	} 

        if ((rc = MQTTClient_unsubscribe(client, sub_topic)) != MQTTCLIENT_SUCCESS)
        {
        	printf("Failed to unsubscribe, return code %d\n", rc);
        	rc = EXIT_FAILURE;
        }
    }

    if ((rc = MQTTClient_disconnect(client, 10000)) != MQTTCLIENT_SUCCESS)
    {
    	printf("Failed to disconnect, return code %d\n", rc);
    	rc = EXIT_FAILURE;
    }
destroy_exit:
    MQTTClient_destroy(&client);
exit:
    return rc;
}
