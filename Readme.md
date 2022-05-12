# PCF85063A Zephyr RTC Driver 

## Usage

Add to your `west.yml` manifest:

```
manifest:
  projects:
    # RTC Driver
    - name: pcf85063a
      url: https://github.com/circuitdojo/pcf85063a
      revision: master
      path: pcf85063a
```

This will import the driver and allow you to use it in your code.

Additionally make sure that you run `west update` when you've added this entry to your `west.yml`.

### Configuration

Add this entry to your .conf

```
# RTC
CONFIG_PCF85063A=y
```

### Overlay

Here is an example of defining the PCF85063A in your .overlay

```
&i2c1 {
	compatible = "nordic,nrf-twim";
	status = "okay";
	sda-pin = <26>;
	scl-pin = <27>;

	pcf85063a@51 {
		compatible = "nxp,pcf85063a";
		label = "PCF85063A";
		reg = <0x51>;
	};
};
```

### Import

For time set/get you will need to include:

```
#include <drivers/rtc/pcf85063a.h>
```
