# About
Originally developed for the Vali/MollenOS operating system, this is a generic package management system that is built as a lightweight alternative to current package managers. Its not only for package management, but also as an application format. 

Chef consists of 3 parts, bake, order and serve.

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/vchef)

## Bake
The bake utility serves as the builder, and orchestrates everything related to generation of bake packages. Bake packages serve both as packages and application images that can be executed by serve.

## Order
Order handles the orchestration of the online segment. Order controls your account setup, downloading of packages, package query and is the gateway to serve

## Serve
Serve is the application backend. This needs to be implemented on an OS basis. This means if you seek to support chef applications, you need to implement serve for your OS.
