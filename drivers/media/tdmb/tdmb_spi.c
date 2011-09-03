#include <linux/spi/spi.h>
#include "tdmb.h"

struct spi_device *spi_dmb;

static int tdmbspi_probe(struct spi_device *spi)
{
	int ret;

	spi_dmb = spi;

	DPRINTK("tdmbspi_probe() spi_dmb : 0x%x \n", spi_dmb);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		DPRINTK("spi_setup() fail ret : %d \n", ret);
		return ret;
	}		

	return 0;
}

static int __devexit tdmbspi_remove(struct spi_device *spi)
{
	return 0;
}

static struct spi_driver tdmbspi_driver = {
	.driver = {
		.name	= "tdmbspi",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= tdmbspi_probe,
	.remove	= __devexit_p(tdmbspi_remove),
};

int tdmb_init_bus(void)
{
	return spi_register_driver(&tdmbspi_driver);
}

void tdmb_exit_bus(void)
{
	spi_unregister_driver(&tdmbspi_driver);
}
