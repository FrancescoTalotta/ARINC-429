import sigrokdecode as srd


class SamplerateError(Exception):
    pass


class SignalPolarityError(Exception):
    pass


class Annotation:
    (BIT, LABEL, SDI, DATA, SSM, PARITY, PARITY_CHECK) = range(7)


class Pin:
    (A, B) = range(2)


class Decoder(srd.Decoder):
    api_version = 3
    id = 'ARINC429'
    name = 'ARINC 429'
    longname = 'ARINC 429 label-only decoder (minimal analog slicer)'
    desc = 'test'
    license = 'mit'
    inputs = ['logic']
    outputs = ['arinc429']
    tags = ['Debug/trace']

    channels = (
        {'id': 'A', 'name': 'a', 'desc': 'ARINC A'},
        {'id': 'B', 'name': 'b', 'desc': 'ARINC B'},
    )

    annotations = (
        ('bit',          'Bits'),
        ('label',        'LABEL'),
        ('sdi',          'SDI'),
        ('data',         'DATA'),
        ('ssm',          'SSM'),
        ('parity',       'PARITY'),
        ('parity_check', 'PARITY CHECK'),
    )

    # IMPORTANT: use integer tuples (indexes into annotations)
    annotation_rows = (
        ('bits',  ' Bits', (0,)),   # 'bits'
        ('word',   'Word', (1, 2, 3, 4, 5, )),   # 'arinc word'
        ('parity', 'Parity Check', (6, )),   # 'parity check'
    )

    def __init__(self):
        self.out_ann = None

        self.bit_block_ss = None
        self.label_block_ss = None
        self.sdi_block_ss = None
        self.data_block_ss = None
        self.ssm_block_ss = None
        self.parity_block_ss = None
        self.parity_check_block_ss = None

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)

    def reset(self):
        self.bit_block_ss = None
        self.label_block_ss = None
        self.sdi_block_ss = None
        self.data_block_ss = None
        self.ssm_block_ss = None
        self.parity_block_ss = None
        self.parity_check_block_ss = None

    def decode(self):

        while True:
         
         parity_check=0
         label_check=True        #<-------To filter set label_check=False
         label_select=71         #<-------Active only if label_check=True

#----------------LABEL-------------------
         for i in range(0, 16):
          self.wait([{0: 'r'}, {0: 'f'}, {1: 'r'}, {1: 'f'}])

          if i == 0:
             label=0b0
             counter = 0
             self.label_block_ss = self.samplenum
             self.parity_check_block_ss = self.samplenum

          if self.matched == (True, False, False, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, True, False, False):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 1', '1']])
             counter = counter + 1
             label |= (1 << 8-counter)
             parity_check = parity_check + 1
          elif self.matched == (False, False, True, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, False, False, True):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 0', '0']])
             counter = counter + 1
          if ((i == 15) and (label==label_select) or label_check and i == 15):
             self.put(self.label_block_ss, self.samplenum, self.out_ann, [Annotation.LABEL, ['LABEL: ' + oct(label), oct(label)]])
             label_check=True
          
#----------------SDI-------------------
         for i in range(0, 4):
          self.wait([{0: 'r'}, {0: 'f'}, {1: 'r'}, {1: 'f'}])

          if i == 0:
             sdi=0b0
             counter = 0
             self.sdi_block_ss = self.samplenum

          if self.matched == (True, False, False, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, True, False, False):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 1', '1']])
             counter = counter + 1
             sdi |= (1 << 2-counter)
             parity_check = parity_check + 1
          elif self.matched == (False, False, True, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, False, False, True):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 0', '0']])
             counter = counter + 1
          
          if i == 3 and label_check:
             sdi = ((sdi & 1) << 1) | ((sdi >> 1) & 1)
             self.put(self.sdi_block_ss, self.samplenum, self.out_ann, [Annotation.SDI, ['SDI: ' + str(int(sdi)), str(int(sdi))]])

#----------------DATA-------------------
         for i in range(0, 38):
          self.wait([{0: 'r'}, {0: 'f'}, {1: 'r'}, {1: 'f'}])

          if i == 0:
             data=0b0
             counter = 0
             self.data_block_ss = self.samplenum

          if self.matched == (True, False, False, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, True, False, False):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 1', '1']])
             counter = counter + 1
             data |= (1 << 19-counter)
             parity_check = parity_check + 1
          elif self.matched == (False, False, True, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, False, False, True):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 0', '0']])
             counter = counter + 1
          
          if i == 37 and label_check:
             self.put(self.data_block_ss, self.samplenum, self.out_ann, [Annotation.DATA, ['DATA: ' + str(hex(data)), str(hex(data))]])

#----------------SSM-------------------
         for i in range(0, 4):
          self.wait([{0: 'r'}, {0: 'f'}, {1: 'r'}, {1: 'f'}])

          if i == 0:
             ssm=0b00
             counter = 0
             self.ssm_block_ss = self.samplenum

          if self.matched == (True, False, False, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, True, False, False):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 1', '1']])
             counter = counter + 1
             ssm |= (1 << 2-counter)
             parity_check = parity_check + 1
          elif self.matched == (False, False, True, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, False, False, True):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 0', '0']])
             counter = counter + 1
          
          if i == 3 and label_check:
             self.put(self.ssm_block_ss, self.samplenum, self.out_ann, [Annotation.SSM, ['SSM: ' + str(int(ssm)), str(int(ssm))]])

#----------------PARITY-------------------
         for i in range(0, 2):
          self.wait([{0: 'r'}, {0: 'f'}, {1: 'r'}, {1: 'f'}])

          if i == 0:
             parity=0b0
             counter = 0
             self.parity_block_ss = self.samplenum

          if self.matched == (True, False, False, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, True, False, False):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 1', '1']])
             parity = 1
             parity_check = parity_check + 1
          elif self.matched == (False, False, True, False):
             self.bit_block_ss = self.samplenum
          elif self.matched == (False, False, False, True):
             self.put(self.bit_block_ss, self.samplenum, self.out_ann, [Annotation.BIT, ['BIT 0', '0']])
             counter = counter + 1
          
          if i == 1 and label_check:
             if parity_check % 2:
               self.put(self.parity_block_ss, self.samplenum, self.out_ann, [Annotation.PARITY, ['PARITY OK',  'OK']])
               self.put(self.parity_check_block_ss, self.samplenum, self.out_ann, [Annotation.PARITY_CHECK, ['PARITY OK',  'OK']])
             else: 
               self.put(self.parity_block_ss, self.samplenum, self.out_ann, [Annotation.PARITY, ['PARITY ERR',  'ERR']])
               self.put(self.parity_check_block_ss, self.samplenum, self.out_ann, [Annotation.PARITY_CHECK, ['PARITY ERR',  'ERR']])
