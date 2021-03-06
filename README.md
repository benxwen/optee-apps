# OP-TEE Benchmarking Algorithms & Building Blocks of MQTT-TZ
## Contents
1. [Introduction](#1-introduction)
2. [List of Implemented Applications](#2-list-of-implemented-applications)

## 1. Introduction
This document describes the different benchmarking applciations developed with the Op-TEE framework together with the implemented min-apps that will pave the way for the MQTT-TZ implementation.

---
## 2. List of Implemented Applications

**Hello World**

Directory **hello_world/**:
* A very simple Trusted Application to answer a hello command and incrementing
an integer value.
* Test application: `optee_example_hello_world`

---

**Symmetric Encryption: AES**

Directory **aes/**:
* Runs an AES encryption and decryption from a TA using the GPD TEE Internal
Core API. Non secure test application provides the key, initial vector and
ciphered data.
* Test application: `optee_example_aes`
* Benchmarking: secure vs non secure encryption and decryption of a 4 kB piece of clear text. 100 times, report avg and std:

---

**Secure Storage**

Directory `secure_storage`, `read-key`, `save-key`:
* A Trusted Application to read/write raw data into the OP-TEE secure storage using the GPD TEE Internal Core API.

---

**TCP Server**

Directory `tcp_server`:
* Application that places a TCP server in the Rich OS. Some things to be considered to get the networking to work with `QEMU`. Right now I am starting the VM with the following configuration parameters (to be configured in `build/common.mk`):
```bash
qemu-system-aarch64 \
        -nographic \
        -serial tcp:localhost:54320 -serial tcp:localhost:54321 \
        -smp 2 \
        -s -S -machine virt,secure=on -cpu cortex-a57 \
        -d unimp -semihosting-config enable,target=native \
        -m 1057 \
        -bios bl1.bin \
        -initrd rootfs.cpio.gz \
        -kernel Image -no-acpi \
        -append 'console=ttyAMA0,38400 keep_bootcon root=/dev/vda2' \
        -netdev user,id=vmnic,hostfwd=tcp::9999-:9999 -device virtio-net-device,netdev=vmnic
```
* Note the host forwarding. This way the client can run in `localhost` and the server in the Rich OS in `QEMU`.
* The client must then listen to `127.0.0.1:9999`.
* The server must bind to `10.0.2.2:9999`, this is due to the fact that we make use of the default `QEMU` [SLIRP netowrk](https://wiki.qemu.org/Documentation/Networking).
* To **run the program** execute the `optee_tcp_server` binary as indicated below, and the client one (`tcp_server/client`) in another terminal in localhost.
* If what is desired is to connect via SSH, two further things need to be done. Firstly, the OPENSSH package must be included. Generally, to include packages that are already preloaded with `buildrrot` one must only do the following:
```bash
echo '@echo "BR2_PACKAGE_OPENSSH=y" >> ../out-br/extra.conf' >> ${OPTEE_SRC}/build
cd ${OPTEE_SRC}/build
make clean
make -j $(nproc)
make run
```
* Lastly, given that the default non-privileged user is `test` and has no password, the `/etc/ssh/sshd_config` file must be edited to allow empty passwords.

---

**Secure Cache Implementation + Proxy Reencryption**

Directory `hot_cache`
+ The aim of this application is to implement a _secure_ hot cache. It is the TA used in the MQT-TZ engine.
+ Ideally it is a TCP server running in the Rich OS that receives queries to either set, or get a key basing on a client id.
+ Keys will be stored in secure storage. We will study the two different types of [secure storage](https://optee.readthedocs.io/architecture/secure_storage.html) availables in `Op-Tee`.
+ The current API is the following:
```c
set_key(client_id,key)
get_key(client_id)
```

---

**Socket Benchmark**

Directories: `socket-benchmark`, `socket-throughput`, `threaded-socket`.
+ This set of directories containes a benchmark of the socket API implementation in Op-TEE and TrustZone.

---

## 3. Run the example applications
The current applciations and results are executed using QEMUv8. For instructions on how to build the distribution go [here](https://optee.readthedocs.io/building/devices/qemu.html#qemu-v8).

Once you have the distribution installed and compiled, go to you project source (`optee-qemuv8` most likely) and replace the existing `optee_examples` with this repository. Then, assuming you are in the project source directory,
```
cd build
make -j `nproc`
make run
```

Ideally, two terminal windows should pop up, first go to the build repo and pres `c`, then from the *Normal* terminal window, type in `root` as login and then `cd /usr/bin`. There you will find all the example applications to be run.
