import json 
import re
import logging
import sys, os
import struct
import binascii
import threading
import time 
from threading import Lock
import binascii #for decode ubx message
from datetime import datetime
import subprocess
from os.path import exists
import io 
import paho.mqtt.client as mqtt
import random
import multiprocessing as mp

DEFAULT_LOG_LEVEL=logging.DEBUG

LOG_TO_FS=False
# LOG_TO_FS=True

MOSQUITTO_LOGFILE = "/var/log/mosquitto/mosquitto.log"
BinaryFileLogName = "binaryLogGnss"
BinaryFileLog = None 

MOSQUITTO_HEALTH_WATCHDOG=10
mosquittoStatus = False #status of mosquitto broker daemon
# client, user and device details
serverUrl   = "mqtt.cumulocity.com"
clientId    = "GcpClient"
device_name = "My Python MQTT device"
tenant      = "<<tenant_ID>>"
username    = "<<username>>"
password    = "<<password>>"

task_queue = mp.Queue()
client = mqtt.Client(clientId)

GcpClientMsgIndex = 0


def on_message(client, userdata, message):
    payload = message.payload.decode("utf-8")
    logging.info("[GcpClient]+++ < received message: {}/{}".format(message.topic, payload))
    # if payload ... :
    #     task_queue.put(...)

def on_publish(client, userdata, mid):
    logging.info("[GcpClient]+++ > published message: {}".format( mid))

def on_connect(mqttc, obj, flags, rc):
    if rc != 0:
        logging.info("[GcpClient]+++ Could not connect to broker! {}".format( rc))
    else:
        logging.info("[GcpClient]+++ Connected to broker! {}".format( rc))
        task_queue.put(send_testing_msg)
        task_queue.put(process_subscribe)

def on_disconnect(mqttc, obj, rc):
    obj = rc
    logging.info("[GcpClient]+++ Broker has disconnected {}".format( rc))

def on_subscribe(mqttc, obj, mid, granted_qos):
    logging.info("[GcpClient]+++ Subscribed:  {} {}".format( mid, granted_qos))


# # simulate restart
# def perform_restart():
#     logging.info("Simulating device restart...")
#     publish("s/us", "501,c8y_Restart", wait_for_ack = True);

#     logging.info("...restarting...")
#     time.sleep(1)

#     publish("s/us", "503,c8y_Restart", wait_for_ack = True);
#     logging.info("...restart completed")

# # send temperature measurement
# def send_measurement():
#     logging.info("Sending temperature measurement...")
#     temperature = random.randint(10, 20)
#     publish("s/us", "211,{}".format(temperature))

# publish a message

def send_testing_msg():
    logging.info("[GcpClient]+++ > sending testing msg")
    publish("v/AaBbCcDdEeFf/default", "Hellooooo2!!!")

def send_ack_msg():
    global GcpClientMsgIndex
    logging.info("[GcpClient]+++ > sending ack msg")
    GcpClientMsgIndex+=1
    publish("v/AaBbCcDdEeFf/default", "ACK:{}".format(GcpClientMsgIndex))

def process_subscribe():
    #subscribe to topic b, which clustercontroller will send message to
    logging.info("[GcpClient]+++ subscribe to b topic")
    client.subscribe("/b/#")

def publish(topic, message, wait_for_ack = False):
    QoS = 2 if wait_for_ack else 0
    message_info = client.publish(topic, message, QoS)
    if wait_for_ack:
        logging.info("[GcpClient]+++ > awaiting ACK for {}".format(message_info.mid))
        message_info.wait_for_publish()
        logging.info("[GcpClient]+++ < received ACK for {}".format(message_info.mid))


# def device_loop():
#     while True:
#         task_queue.put(send_measurement)
#         time.sleep(7)

def MqttClientThread():
    global client

    client.on_message = on_message
    client.on_publish = on_publish
    client.on_connect = on_connect
    client.on_disconnect  = on_disconnect 
    client.on_subscribe = on_subscribe

    client.connect('localhost',1883,60)
    client.loop_start()

    #loop process publish message
    while True:
        task = task_queue.get()
        task()

def MosquittoLoggingThread():
    while True:
        if exists(MOSQUITTO_LOGFILE) == False:
            logging.info("[{}] File not exist or not ready, retry after 1s".format(sys._getframe().f_code.co_name))
            time.sleep(1)
            continue
        else:
            logging.info("[{}] File ready".format(sys._getframe().f_code.co_name))
            break

    LogIo = None
    while True:
        if LogIo == None:
            LogIo = io.open(MOSQUITTO_LOGFILE, mode='r', buffering=-1, encoding=None, errors=None, newline=None, closefd=True)
        if LogIo.readable() == True:
            logLine = LogIo.readline()
            if len(logLine) > 0:
                logging.info("[GcpBroker]---{}".format(logLine ))


def MosquittoHealthThread():
    while True:
        # logging.info("Watch dog rx thread")
        time.sleep(MOSQUITTO_HEALTH_WATCHDOG)
        mosquittoStatus = os.system('systemctl is-active --quiet mosquitto')
        if mosquittoStatus == 0 :
            mosquittoUpTime=subprocess.check_output(['systemctl', 'show','mosquitto','--property=ActiveEnterTimestamp']).decode().strip()
            logging.info("[{}] Mosquitto is active, {}".format( (sys._getframe().f_code.co_name), mosquittoUpTime ))
        else:
            logging.info("[{}] Mosquitto is inactive".format(sys._getframe().f_code.co_name))
        task_queue.put(send_ack_msg)

def main():
    if not LOG_TO_FS:
        logging.basicConfig(level=DEFAULT_LOG_LEVEL,
                        format='%(asctime)s - %(levelname)s - %(message)s')    
    else:
        logname = str(datetime.now())
        logname = logname.replace("-","").replace(" ","-").replace(":","").replace(",","").replace(".","")
        logname = "./log/MqttManager-"+logname+".txt"
        logging.basicConfig(filename=logname,
                        filemode='a',
                        level=DEFAULT_LOG_LEVEL,
                        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')    

    logging.info("Hello mqtt broker!")

    if exists("/var/log/mosquitto") == False:
        os.mkdir("/var/log/mosquitto")
    if exists("/var/log/mosquitto/history") == False:
        os.mkdir("/var/log/mosquitto/history")

    shutDownBeforeRun = os.system('sudo systemctl stop mosquitto')
    time.sleep(0.2)
    backUpLogFile = os.system('sudo logrotate -f /etc/logrotate.d/mosquitto_log.conf')
    time.sleep(0.2)
    startBroker = os.system('sudo systemctl restart mosquitto')
    time.sleep(0.2)


    mMosquittoHealthThread = threading.Thread(target=MosquittoHealthThread, daemon=True)
    mMosquittoLoggingThread = threading.Thread(target=MosquittoLoggingThread, daemon=True)
    mMqttClientThread = threading.Thread(target=MqttClientThread, daemon=True)

    mMosquittoHealthThread.start()
    mMosquittoLoggingThread.start()
    mMqttClientThread.start()

    # txTh.join()
    # rxTh.join()
    try:
        while True: 
            pass     
    except KeyboardInterrupt:
        logging.info('You pressed ctrl+c')

if __name__ == '__main__':
    main()
