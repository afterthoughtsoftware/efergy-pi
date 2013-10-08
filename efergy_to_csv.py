import time
import sys
from datetime import datetime

DEBUG = False
ID1 = 0x09
ID2 = 0x94

def pr_debug(line):
  if DEBUG:
    sys.stderr.write(line)

def is_checksum_good(data):
  checksum = 0;
  for i in range(7):
    checksum += data[i]
    pr_debug("0x{0:02x} ".format(data[i]))

  pr_debug("checksum: 0x{0:02x}\n".format(checksum % 256))

  return data[7] == checksum % 256

def get_data(fh):
  data = []
  while True:
    c = fh.read(1)
    if c == "":
      time.sleep(0.5)
      continue
    pr_debug("Data: 0x{0:02x}\n".format(ord(c)))

    data.append(ord(c))
    if len(data) < 8:
      continue

    if data[0] == ID1 and data[1] == ID2 and is_checksum_good(data):
      return data;
    else:
      data.pop(0)


if __name__ == "__main__":
  with open("power.csv", "a") as csv:
    with open("/dev/efergy_device", "r") as fh:
      while True:
        data = get_data(fh)
        current = ((data[4] << 8) | data[5]) / 32768.0 * (2 ** data[6])
        power = current * 240
        csvdata = ",".join([str(datetime.date(datetime.now())), str(datetime.time(datetime.now())), str(current), str(power)])
        print csvdata
        csv.write(csvdata)
        csv.write("\n")
        csv.flush()
