from __future__ import division
import sys
import os
import linecache




def sizeof_fmt(num, suffix='B'):
    for unit in ['','Ki','Mi','Gi','Ti','Pi','Ei','Zi']:
        if abs(num) < 1024.0:
            return "%3.1f%s%s" % (num, unit, suffix)
        num /= 1024.0
    return "%.1f%s%s" % (num, 'Yi', suffix)

#What interval should be used on x-axis. I.e., how many ms should one tick
#express
# but express interval length in number of packets
interval_length = int(sys.argv[1])
logfile = open(sys.argv[2], "r")

#Read the initial timestamp, so I can calculate the first timestamp
line = logfile.readline().strip().split(' ')
initial_tstamp = float(line[0])

cur_interval_start = initial_tstamp

#Timestamp in file is in seconds
#cur_interval_end = initial_tstamp + (interval_length / 1000.0)

cur_interval_end= float(linecache.getline(sys.argv[2], int(sys.argv[1])).strip().split(' ')[0])
#Reset file descriptor
logfile.seek(0, os.SEEK_SET)
read = True
interval_bytes = 0
x_val=0
num_packets=0

while True:
    #To avoid the additional function, have a check here for read. This is set
    #to false when a new interval is started
    if read:
        line = logfile.readline()

        if line == '':
            break

        line = line.strip().split(' ')
    
    tstamp, numbytes = float(line[0]), int(line[1])
    num_packets=num_packets+1  
    

    if tstamp >= cur_interval_end:
	x_val=cur_interval_end-cur_interval_start
        print x_val, sizeof_fmt(interval_bytes), ((interval_bytes*8)/(1024*1024))/x_val 
		     
        interval_bytes = 0
        #read = False
        if num_packets < (int(sys.argv[1])*int(sys.argv[3])):
		cur_interval_start= float(linecache.getline(sys.argv[2], interval_length+1).strip().split(' ')[0])
	interval_length=interval_length+int(sys.argv[1])
	if num_packets < (int(sys.argv[1])*int(sys.argv[3])):
		cur_interval_end= float(linecache.getline(sys.argv[2], interval_length).strip().split(' ')[0])
    else:
        #if tstamp > 1357573434.78 and tstamp < 1357573434.88:
        #    print tstamp, cur_interval_start, cur_interval_end

        read = True
        interval_bytes += numbytes
	#print tstamp, interval_bytes 
     

logfile.close()
