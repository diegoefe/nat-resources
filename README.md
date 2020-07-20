# NAT resources

| Type    | Link | More |
| ------- | -----| ---|
| Paper   | [Peer-to-Peer Communication Across Network Address Translators](http://www.brynosaurus.com/pub/net/p2pnat/) | |
| Demo    | [How to communicate peer-to-peer through NAT (Network Address Translation) firewalls](http://www.mindcontrol.org/~hplus/nat-punch.html) | [source](http://www.mindcontrol.org/%7Ehplus/nat-punch.zip),  [local fork](nat-punch) |


## Libraries
| Library | Version | Website | Link | Notes |
| -------| --------| -----| -----| ----|
| pjsip | 2.7.2 | [home](http://www.pjsip.org) | [download](http://www.pjsip.org/download.htm) | |
| nice | 0.1.14.1 | [home](https://gitlab.freedesktop.org/libnice/libnice/) | [download](https://gitlab.freedesktop.org/libnice/libnice/-/archive/master/libnice-master.tar.gz) | Requires glib/gobject/gthread/gio|
| libre | 0.5.8 | [home](http://www.creytiv.com) | [download](http://www.creytiv.com/pub) | ICE does not support TCP |
| sourcey | 1.1.4 | [home](https://github.com/sourcey/libsourcey/) | [download](https://github.com/sourcey/libsourcey/archive/1.1.4.tar.gz) | |
| reciprocate | 1.10.2 | [home](https://github.com/resiprocate/resiprocate) | [download](https://github.com/resiprocate/resiprocate/archive/resiprocate-1.10.2.tar.gz) | |

[List of public STUN servers](stun_servers.txt)


## Notes on installation of dependencies
- On Ubuntu
    - pjsip dependencies:
    ```bash
    sudo apt-get install uuid-dev
    sudo apt-get install libssl-dev
    ```
    - nice dependencies:
    ```bash
    sudo apt-get install gtk-doc-tools
    ```
