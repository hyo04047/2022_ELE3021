#!/bin/zsh

qemu-system-i386 -nographic -serial mon:stdio -smp 1 -m 512 -drive file=fs.img,index=1,media=disk,format=raw -drive file=xv6.img,index=0,media=disk,format=raw
