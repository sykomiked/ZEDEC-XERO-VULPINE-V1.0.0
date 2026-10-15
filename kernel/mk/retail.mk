# Retail track and people features (R1.4-R1.5, R2.2-R2.6, R3.1, R3.4-R3.6,
# R4.4-R4.5, people features in the app). Each area keeps its own fragment
# so kernel/Makefile only carries the include and one verify-all line.
include mk/rot.mk
include mk/compliance.mk
include mk/edge.mk
include mk/bearer.mk
include mk/people.mk

.PHONY: test-retail
test-retail:
	@echo "=== retail: boot attestation, zero-touch provisioning, QR (R1.4-R1.5) ==="
	@$(MAKE) --no-print-directory test-rot
	@echo "=== retail: capital controls, tax hooks, KYC wrapper, off by default (R3.4-R3.6) ==="
	@$(MAKE) --no-print-directory test-compliance
	@echo "=== retail: edge inference node package and edge cluster (R4.4-R4.5) ==="
	@$(MAKE) --no-print-directory test-edge
	@echo "=== retail: BLE, Wi-Fi Direct, LoRa, serial, modem, cellular and ham bearers (R2.2-R2.6) ==="
	@$(MAKE) --no-print-directory test-bearer
	@echo "=== people: feed, calls, translation, devices and simple mode in zxvd (4.11, R3.1) ==="
	@$(MAKE) --no-print-directory test-people
