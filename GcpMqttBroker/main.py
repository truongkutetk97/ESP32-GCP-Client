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
from sh import tail
from os.path import exists
import io 

DEFAULT_LOG_LEVEL=logging.DEBUG

LOG_TO_FS=False
# LOG_TO_FS=True

MOSQUITTO_LOGFILE = "/var/log/mosquitto/mosquitto.log"
BinaryFileLogName = "binaryLogGnss"
BinaryFileLog = None 

mosquittoStatus = False #status of mosquitto broker daemon


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
                logging.info("[{}]---{}".format(sys._getframe().f_code.co_name,logLine ))


def MosquittoHealthThread():
    while True:
        # logging.info("Watch dog rx thread")
        time.sleep(1)
        mosquittoStatus = os.system('systemctl is-active --quiet mosquitto')
        if mosquittoStatus == 0 :
            mosquittoUpTime=subprocess.check_output(['systemctl', 'show','mosquitto','--property=ActiveEnterTimestamp']).decode().strip()
            logging.info("[{}] Mosquitto is active, {}".format( (sys._getframe().f_code.co_name), mosquittoUpTime ))
        else:
            logging.info("[{}] Mosquitto is inactive".format(sys._getframe().f_code.co_name))

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
    mMosquittoHealthThread.start()
    mMosquittoLoggingThread.start()


    # txTh.join()
    # rxTh.join()
    try:
        while True: 
            pass     
    except KeyboardInterrupt:
        logging.info('You pressed ctrl+c')

if __name__ == '__main__':
    main()
