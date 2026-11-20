"""Run with KiCad's Python after exporting a KiCad XML netlist to build/v2-light.net."""
from pathlib import Path
import xml.etree.ElementTree as ET
import pcbnew

xml = ET.parse('build/v2-light.net').getroot()
nets = {n.attrib['name']: {(p.attrib['ref'], p.attrib['pin']) for p in n.findall('node')}
        for n in xml.find('nets')}
assert nets['/LIGHT_ADC_GPIO4'] == {('U1','8'),('R5','2'),('C1','1'),('J6','1'),('R6','1')}
assert nets['/LIGHT_EXCITE_GPIO14'] == {('U1','16'),('R5','1')}
assert {('C1','2'),('J6','2'),('R6','2'),('Q2','1')} <= nets['/GND']
assert ('U1','4') in nets['/PIR_WAKE_GPIO0']
assert ('U1','10') in nets['/PERIPH_EN_GPIO22']
assert ('U1','9') in nets['/VOLUME_ADC_GPIO5']
assert ('Q2','3') in nets['/GATE_PULLDOWN']
board = pcbnew.LoadBoard('resources/schematics/schematics.kicad_pcb')
actual = {(f.GetReference(),p.GetNumber()):p.GetNetname() for f in board.GetFootprints() for p in f.Pads()}
for net,nodes in nets.items():
    for key in nodes:
        if key in actual: assert actual[key] == net, (key, net, actual[key])
for ref in ['R5','C1','J6']:
    assert any(f.GetReference() == ref for f in board.GetFootprints())
assert not any(f.GetReference() == 'R6' for f in board.GetFootprints())
assert len(board.GetTracks()) == 0
print('PASS: divider, wake, control, volume and Q2 nets; PCB matches schematic; external LDR excluded; board unrouted.')
