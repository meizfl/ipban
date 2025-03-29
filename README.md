# IPBan
IPBan - program for blocking IP addresses and IP subnets based on iproute2 in accordance with the config /etc/ipban/routes.toml

Using:
========
example config:
```
[ipv4_routes]
routes = ["78.36.0.0/15", "178.176.0.0/14", "178.64.0.0/13"]

[ipv6_routes]
routes = ["2a01:620::/32", "2a00:1e88::/32", "2a00:56c0::/32", "2a00:62c0::/32", "2a00:b440::/32", "2a01:540::/32", "2a02:ad8::/32", "2a03:d000::/29"]
```
the config is located at /etc/ipban/routes.toml
