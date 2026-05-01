# CS Project Specilization - Raspberry Pi Local SoftAP Setup

We got inspired to make this project because we realised how many people lose their life or get their life dramaticly changed for the worse because of PFM -1 anti personal landmines.

## 0 - Before you begin
Flash the sd card with raspberry pi image matching the model you are going to use

# how to connect to it easly 
...
### Before you can use the raspberry pi remember to run
Update the system with the follwing commands or if you are in desktop mode you can use the update program in the topbar
```
sudo apt update
sudo apt upgrade
```
## 01 - Install required packages
For the raspberry pi to be able to turn on soft AP mode, you need two packages:
hostapd to create the Wi‑Fi AP
dnsmasq to hand out local IP addresses
Run these commands to download the needed packages:
```
sudo apt install hostapd dnsmasq
```
hostapd is be default masked, that just means it can run so we need to unmask it, you can always undo it 
```
sudo unmask hostapd
```
We need to configure the configs of the packages before we can use them. For that to be possible we stop them so they are not running
sudo systemctl stop hostapd dnsmasq

## 02 - Assign a static IP to wlan0
Your Pi must have a fixed address for the SoftAP network.
If you are using an older Raspberry pi you might need to edit /etc/dhcpcd.conf
Create of Edit this file if it already exist, it's the same process
```
sudo nano /etc/systemd/network/filename.network
```
Then add the following configuration, the address can almost be anything
```
[Match]
Name=wlan0
[Network]
# give it a static address you can choose whatever it just has to be under 256
Address=192.168.4.1/24
ConfigureWithoutCarrier=yes
```


## 03 - Configure dnsmasq for local‑only DHCP
This gives your other device an IP but.
Create of Edit this file if it already exist, it's the same process
```
sudo nano /etc/dnsmasq.conf
```
Then add the following configuration, remember to have the same base address as you use in the dhcp-range as you set it to in the .network file 

```
# This gives other devices an IP but does not forward the internet
interface=wlan0

dhcp-range=192.168.4.10,192.168.4.10,255.255.255.0/24h
```

The two numbers you pick here 192.168.50.10 and 192.168.50.10
Is all of the IP addresses you can give out in this network. This does not restrict other users to get access to the network though. Here there is only one IP to give out 192.168.50.14 would be 5 IP’s to give out. 
This creates a tiny LAN with no upstream gateway.

## 04 - Create the hostapd Wi‑Fi access point
This defines your SSID, password, and Wi‑Fi mode and handles the wifi security, here we use wpa2. Below is an example of a configuration, we used one like this one
Create a new file called hostapd.conf
```
sudo nano /etc/hostapd/hostapd.conf
```
Here is an example of a config you can use we used one like this. This config uses WPA2:
```
interface=wlan0
driver=nl80211
ssid=name_of_wifi
hw_mode=g
channel=6
auth_algs=1
wpa=2
wpa_passphrase=a_password_that_is_minimum_8_character
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
```

Afterwards you need to point to this file in /etc/default/hostapd 
```
sudo nano /etc/default/hostapd
```
Change the DAEMON_CONF="” to the line below
```
DAEMON_CONF="/etc/hostapd/hostapd.conf"
```
## 05 - Start services and test
Here you have two options, one is where the raspberry pi has a ethernet connection and the other one is where we only use wifi. 
Only wifi can cause more problems because it’s hardware to troubleshoot.

You need to turn off the systems that enables the device to be a wifi client aka the ability to connect to a wifi network. When this is done you will lose connection to the device if you are using wifi. If you are on ethernet its fine you will keep the connection.

You need to restart the systems we have configured now so the new settings will be read.
```
sudo systemctl restart systemd-networkd
sudo systemctl restart hostapd
sudo systemctl restart dnsmasq
```
Then because a system called wpa_supplicant will try to use wlan0 we need to disable it, when this is done you WILL lose connection if you are on a wifi connection to the raspberry pi. 
```
sudo systemctl stop wpa_supplicant
sudo systemctl stop NetworkManager 2>/dev/null || true
```
This is a temporary disable of the wpa_supplicant so you wont lose access permanently, after a reboot it will be enabled again

If you want to disable this permanently run this command, dont this unless you are on ethernet or are sure that you wont need it anymore.

sudo mask wpa_supplicant
 
If you do this on wifi you need to run them all together like this.
```
sudo systemctl stop wpa_supplicant
sudo systemctl stop NetworkManager 2>/dev/null || true
sudo systemctl restart systemd-networkd
sudo systemctl restart hostapd
sudo systemctl restart dnsmasq
```
Now you should be able to find you network and be able to connect to it and ssh to the raspberry again.
