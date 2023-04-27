#!/bin/bash
# 使用以下配置可打印读写锁日志
# export CFLAGS="-ggdb3 -O0 -DSWITCH_DEBUG_RWLOCKS"
# export CXXFLAGS="-ggdb3 -O0 -DSWITCH_DEBUG_RWLOCKS"
export CFLAGS="-ggdb3 -O0"
export CXXFLAGS="-ggdb3 -O0"
./bootstrap.sh -j
./configure $@

