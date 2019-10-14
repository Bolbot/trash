sudo /home/box/final/update.sh
cd final
sudo cmake .
sudo make
cd ..
sudo /home/box/final/final -h 127.0.0.1 -p 12345 -d /tmp/
