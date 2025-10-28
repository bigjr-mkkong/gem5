#! /bin/zsh

docker run -u $UID:$GID \
    --volume /home/michael/Projects/pimtlb/gem5:/gem5 \
    --volume /home/michael/Projects/pimtlb/sw-payload:/sw-payload \
    --rm -it gem5env
