## ChatFS: A FUSE-based File System Interface for Chat Services

### Description

A basic RAM-based file system is implemented using FUSE.
It can act as a normal file system, but the files are actually stored in memory.

Besides, it can also be used as an interface for chat services.

File permissions are implemented as a bonus.

### Scripts

Use `make` to build `chatfs`.

To demonstrate the chat service, you can run `./alice` and `./bob` in different terminals to start two ChatFS.
Then run `./run_test`. It will show the procedure of chatting between `alice` and `bob`.

### How to chat

When you start the file system, it will ask you to input your username and port number.
Then it will listen to the port and wait for incoming connections.

There is a special file `/connect` in the file system.
You can write the IP address and port number (separated by a space) of another ChatFS to it.
Then the ChatFS will try to connect to the other ChatFS, and create a file for it.

Say, if you have one ChatFS named `alice` listening on port 5555,
and another named `bob` listening on port 5556.
Both of them are running on the same machine, so the IP address is `127.0.0.1`.
Then you connect `alice` to `bob`, by writing `127.0.0.1 5556` to `/connect` in `alice`'s file system.
If the connection is successful, you will see a new file `/bob` in `alice`'s file system,
and a new file `/alice` in `bob`'s file system.
`alice` can send messages to `bob` by writing to `/bob` in `alice`'s file system, and the content will be written to the file `/alice` in `bob`'s file system.
