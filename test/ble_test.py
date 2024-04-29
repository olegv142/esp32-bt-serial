import sys
import time
import random
import serial
from serial import Serial

serial_baud_rate = 19200
serial_parity    = serial.PARITY_EVEN

max_len  = 512
msg_delay = .1

random.seed()

def random_str(sz):
	buff = ''
	for i in range(sz):
		v = random.randrange(ord('A'), ord('Z') + 1)
		buff += chr(v)
	return buff

def read_resp(com, tout=1):
	resp = b''
	dline = time.time() + tout
	while True:
		r = com.read(max_len)
		if r:
			resp += r
			if resp[-1] == term_chr:
				return resp
			continue
		if time.time() > dline:
			return resp

total_bytes = 0
start = time.time()

with Serial(sys.argv[1], baudrate=serial_baud_rate, parity=serial_parity, timeout=.1) as com:
	try:
		while True:
			s = random_str(random.randrange(1, max_len))
			msg = '#' + s + '_' + s
			com.write(msg)
			resp = read_resp(com)
			total_bytes += len(msg) + len(resp)
			print '.',
			time.sleep(msg_delay)
	except KeyboardInterrupt:
		now = time.time()
		print '\n%u bytes transferred (%u bytes/sec)' % (total_bytes, int(total_bytes/(now-start)))
		pass
