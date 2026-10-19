      *> Declarative corridor data. Integer fields preserve the repeating
      *> assurance rate exactly; no decimal percentage is used as authority.
      *> This copybook defines record shapes, not authorization or custody.
       01  CORRIDOR-AGREEMENT.
           05  AGREEMENT-VERSION  PIC 9(04) COMP-3.
           05  NODE-A-ID          PIC 9(03) COMP-3.
           05  NODE-B-ID          PIC 9(03) COMP-3.
           05  BASE-VBILL-ANCHOR  PIC 9(18) COMP-3.
           05  COMPRESSION-KAPPA  PIC 9(05) COMP-3.
           05  ASSURANCE-FEE-NUM  PIC 9(04) COMP-3.
           05  ASSURANCE-FEE-DEN  PIC 9(04) COMP-3.
           05  COLLATERAL-LIMIT   PIC 9(18) COMP-3.
