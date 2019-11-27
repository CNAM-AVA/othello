if [ "$#" -ne 1 ]; then
    echo "Usage: run <port>";
    exit 1;
fi

if [ "$1" -eq 6000 ]; then 
    echo "Can't use port 6000";
    exit 1;
fi

# Open XQuartz and listen on port 6000
DISPLAY_MAC=`ifconfig en0 | grep "inet " | cut -d " " -f2`:0
if [ -z "$(ps -ef|grep XQuartz|grep -v grep)" ] ; then
    open -a XQuartz
    socat TCP-LISTEN:6000,reuseaddr,fork UNIX-CLIENT:\"$DISPLAY\" &
fi

docker run --rm -it --network host -e DISPLAY=$DISPLAY_MAC -v $(pwd):/tmp/src othello:latest ./othello $1

# Kill the docker display socket
killall socat

# Kill XQuartz
pkill -9 -f XQuartz 