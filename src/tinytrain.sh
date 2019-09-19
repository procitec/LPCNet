#!/bin/bash -x
# tinytrain.sh
# train using a tiny database, synthesis a few samples from within
# training database.  Used to perform quick sanity checks with a few hrs training
#
# usage:
#   $ cd LPCNet/build_linux
#   $ ../src/tinytrain.sh

# TODO:
# + maybe rname to a separate train script so we can repeat in future
# + test utterance of Peter and I
# + this is an unusual test of very small training database, to see if it can
#   work within this

SRC1=david_16kHz.wav             # 122s
SRC2=vk5apr_recording_21_may.wav # 64s
SRC=train_src

DATE=190806b

synth() {
   ./src/dump_data --mag --test --c2pitch --c2voicing ~/Downloads/$1.sw $1.f32 
  ./src/test_lpcnet --mag $1.f32 "$2".raw
}

train() {
    sox ~/Downloads/$SRC1 ~/Downloads/$SRC2 -t sw $SRC.sw
    ./src/dump_data --mag --train --c2pitch --c2voicing -z 1 -n 1E6 $SRC.sw $SRC.f32 $SRC.pcm
    ../src/train_lpcnet.py $SRC.f32 $SRC.pcm lpcnet_$DATE
    ../src/dump_lpcnet.py lpcnet_"$DATE"_10.h5
    cp nnet_data.c src
    make test_lpcnet
}

#train
#synth c01_01 $DATE'_f'
#synth mk61_01 $DATE'_m'
#synth wia $DATE'_wia'
synth bob $DATE'_bob'
