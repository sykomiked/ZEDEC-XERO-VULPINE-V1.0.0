      *> SPDX-License-Identifier: Apache-2.0
       identification division.
       program-id. corridor-record.
       environment division.
       input-output section.
       file-control.
           select sample-file assign to "build/qa/corridor-cobol.bin"
               organization is sequential.
       data division.
       file section.
       fd sample-file.
       01 sample-record pic x(36).
       working-storage section.
       copy "corridor.cpy".
       procedure division.
           move 1 to agreement-version
           move 566 to node-a-id
           move 404 to node-b-id
           move 1000000 to base-vbill-anchor
           move 10000 to compression-kappa
           move 8 to assurance-fee-num
           move 9000 to assurance-fee-den
           move 500000 to collateral-limit
           open output sample-file
           write sample-record from corridor-agreement
           close sample-file
           goback.
