FROM debian:11

# https://pdos.csail.mit.edu/6.828/2020/tools.html
RUN apt-get update && \
    apt-get install -y git build-essential gdb-multiarch qemu-system-misc \
                    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu

WORKDIR /xv6
CMD [ "make", "qemu" ]
