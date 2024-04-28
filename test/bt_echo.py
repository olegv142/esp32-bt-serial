import sys
import bluetooth
import random
import time

max_msg_len = 17*1024
max_msg_offset = 1024
msg_buff = None
idle_delay = .001

def msg_init():
	global msg_buff
	msg_buff = bytearray(random.randrange(256) for _ in xrange(max_msg_offset + max_msg_len))

def msg_random():
	len = random.randrange(max_msg_len+1)
	off = random.randrange(max_msg_offset+1)
	return str(msg_buff[off:off+len])

def find_address(dev_name):
	dev_list = bluetooth.discover_devices(lookup_names = True)
	for (addr, name) in dev_list:
		if name == dev_name:
			return addr
	return None

def receive(sock, size, tout = 5.):
	chunks, sz = [], size
	deadline = None
	while sz > 0:
		data = sock.recv(sz)
		if not data:
			if deadline is None:
				deadline = time.time() + tout
			elif time.time() > deadline:
				raise RuntimeError('receive timeout, %u out of %u bytes received' % (size - sz, size))
			time.sleep(idle_delay)
			continue		
		chunk_sz = len(data)
		if chunk_sz == size:
			return data
		deadline = None
		chunks.append(data)
		sz -= chunk_sz
	return ''.join(chunks)

def chk_resp(msg, resp):
	if msg == resp:
		return True
	print >> sys.stderr, 'bad response:'
	cnt = 0
	for i, sent in enumerate(msg):
		if sent != resp[i]:
			print >> sys.stderr, '[%u] sent %02x, recv %02x' % (i, ord(sent), ord(resp[i]))
			cnt += 1
	print >> sys.stderr, '%u of %u bytes mismatched' % (cnt, len(msg))
	return False

def do_test(addr):
	sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
	sock.connect((addr, 1))
	sock.setblocking(False)

	print >> sys.stderr, 'connected'

	msg_init()
	start = time.time()
	bytes = 0

	while True:
		msg = msg_random()
		sock.send(msg)
		msg_len = len(msg)
		resp = receive(sock, msg_len)
		assert len(resp) == msg_len
		if not chk_resp(msg, resp):
			break
		bytes += msg_len
		print >> sys.stderr, '.',
		if bytes > 1000000:
			now = time.time()
			print >> sys.stderr
			print >> sys.stderr, int(bytes * 8 / (now - start)), 'bits/sec'
			bytes, start = 0, now

if __name__ == '__main__':
	args = sys.argv[1:]
	if not args:
		print >> sys.stderr, 'device name argument is required'
		sys.exit(1)
	dev_name = args[0]
	addr = find_address(dev_name)
	if not addr:
		print >> sys.stderr, 'not found'
		sys.exit(-1)
	if '--lookup' in args[1:]:
		print addr
		sys.exit(0)
	do_test(addr)
