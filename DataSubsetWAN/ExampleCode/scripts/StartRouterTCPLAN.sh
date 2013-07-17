#!/bin/sh

filename=$0
script_dir=`dirname $filename`
executable_name="rtiroutingservice"

if [ -f $ROUTER_HOME/scripts/$executable_name ]
then
    cd $script_dir/../routing
    $ROUTER_HOME/scripts/$executable_name \
	-cfgFile Routing-TCP-LAN.xml \
	-cfgName $1
else
    echo "*****************************************************************"
    echo "The ROUTER_HOME environment variable must be set to"
    echo "Connext_install_dir/RTI_Routing_service_5.0.0 to run this example"
    echo "*****************************************************************"
fi
