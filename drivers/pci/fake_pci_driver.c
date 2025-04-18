#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>

// lspci -nn, hailo's ID
#define VENDOR_ID 0x1e60
#define DEVICE_ID 0x2864

static struct pci_device_id fake_pci_ids[] = {
    { PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, fake_pci_ids);

static int fake_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    int ret;
    dma_addr_t dma_handle, invalid_dma;
    void *dma_buf, *invalid_buf;
    size_t size = 16;

    // Enable PCI device
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable device\n");
        return ret;
    }

    // Allocate DMA buffer within restricted pool
    // --- Test 1: Valid allocation (within restricted pool) ---
    dma_buf = dma_alloc_coherent(&pdev->dev, size, &dma_handle, GFP_DMA32);
    if (!dma_buf) {
        dev_err(&pdev->dev, "DMA allocation failed\n");
        pci_disable_device(pdev);
        return -ENOMEM;
    }

    dev_info(&pdev->dev, "Allocated DMA buffer at 0x%llx (size=%zu)\n",
            (unsigned long long)dma_handle, size);

    // Verify address is within restricted region
    if (dma_handle >= 0xC0000000 && dma_handle + size <= 0xC3FFFFFF) {
        dev_info(&pdev->dev, "  --> Address is WITHIN restricted DMA pool\n");
    } else {
        dev_err(&pdev->dev, "  --> ERROR: Address is OUTSIDE restricted pool!\n");
    }

    // Free DMA buffer (optional: keep for device operations)
    dma_free_coherent(&pdev->dev, size, dma_buf, dma_handle);

    // --- Test 2: Invalid allocation (outside restricted pool) ---
    // [    2.429455] fake_pci_driver 0000:01:00.0: Invalid DMA: 0xc0001000 (size=16)
    // [    2.429463] fake_pci_driver 0000:01:00.0:   --> ERROR: WITHIN restricted pool!
    //  This happens because the kernel's DMA subsystem is enforcing the restricted DMA region for the device, and all DMA allocations for this device are being constrained to that range.

    invalid_buf = dma_alloc_coherent(&pdev->dev, size, &invalid_dma, GFP_KERNEL);
    if (!invalid_buf) {
        dev_info(&pdev->dev, "Invalid DMA allocation blocked (expected)\n");
    } else {
        dev_info(&pdev->dev, "Invalid DMA: 0x%llx (size=%zu)\n", 
                (u64)invalid_dma, size);
        if (invalid_dma >= 0xC0000000 && invalid_dma + size <= 0xC3FFFFFF) {
            dev_err(&pdev->dev, "  --> ERROR: WITHIN restricted pool!\n");
        } else {
            dev_err(&pdev->dev, "  --> ERROR: OUTSIDE restricted pool (IOMMU/SWIOTLB should block!)\n");
        }
        dma_free_coherent(&pdev->dev, size, invalid_buf, invalid_dma);
    }

    invalid_buf = dma_direct_alloc_pages(&pdev->dev, size, &invalid_dma, GFP_KERNEL, 0);
    if (!invalid_buf) {
        dev_info(&pdev->dev, "Direct DMA allocation failed\n");
    } else {
        dev_info(&pdev->dev, "Direct DMA: 0x%llx (size=%zu)\n",
                 (u64)invalid_dma, size);
        if (invalid_dma >= 0xC0000000 && invalid_dma + size <= 0xC3FFFFFF) {
            dev_err(&pdev->dev, "  --> ERROR: WITHIN restricted pool!\n");
        } else {
            dev_err(&pdev->dev, "  --> SUCCESS: OUTSIDE restricted pool!\n");
        }
        dma_direct_free_pages(&pdev->dev, size, invalid_buf, invalid_dma, 0);
    }

    pci_disable_device(pdev);
    return 0;
}

#if 0
static struct of_device_id fake_pci_of_match[] = {
    { .compatible = "fake-pci-device" },
    { }
};
MODULE_DEVICE_TABLE(of, fake_pci_of_match);
#endif

static struct pci_driver fake_pci_driver = {
    .name = "fake_pci_driver",
    .probe = fake_pci_probe,
    .id_table = fake_pci_ids,
};

module_pci_driver(fake_pci_driver);
MODULE_LICENSE("GPL");
