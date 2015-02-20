from datetime import datetime, timedelta
import os
import re


PICTURES_PATH = ''
REFER_TIME = int(datetime.timestamp(datetime.now() - timedelta(days=1)))


snaps = [snap for snap in os.listdir(PICTURES_PATH)
         if re.search('snap_\d.jpg', snap)]

files_2_rm = [snap for snap in snaps if re.findall('\d+', snap).pop()
              < REFER_TIME]

for snap in files_2_rm:
    os.remove(PICTURES_PATH + snap)
