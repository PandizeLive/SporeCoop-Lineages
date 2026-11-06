"""Qualify one armed city turret on a disposable Clyth save with two founders and a rival."""
import argparse
from pathlib import Path
import re
import struct

from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_vehicle_combat import health
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def turret_list(peer, city):
    begin, end = struct.unpack('<II', mem(peer, city + 0x354, 8))
    assert 0 <= end-begin <= 32*4 and (end-begin) % 4 == 0
    return [u32(peer, slot) for slot in range(begin, end, 4)]


def shoot(peer, turret, target, faction):
    before = health(peer, target, 0x540)
    assert before > 20
    combat = target + 0x508
    damage = u32(peer, u32(peer, combat) + 0x18)
    command(peer, f'call {damage:#x} thiscall {combat:#x} f:10 {faction:#x} 0 '
                  f'{turret+0x38:#x} {turret+0x588:#x}')
    return before - health(peer, target, 0x540)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=int)
    parser.add_argument('--guest', type=int)
    args = parser.parse_args()
    peer = Console(args.host)
    wait_until(lambda: peer.call('ping')['ok'], 'No QA console', 60)
    wait_until(lambda: 'GalaxyGameEntry' in command(peer,'game.mode'), 'No galaxy menu', 90)
    assert 'duoqa' in command(peer,'coop.savedb').lower()
    command(peer,'wait.frames 600 120000')
    command(peer,'coop.load Game0 Clyth')
    wait_until(lambda: 'Civilization' in peer.call('coop.avatar')['out'], 'Missing Civilization save', 90)
    location = re.search(r'location=(.+?)\s+access=',command(peer,'coop.savedb'))[1]
    tag, _, _, home, partner = struct.unpack('<4s4I',(Path(location)/'clyth.spo.duonations').read_bytes())
    assert tag == b'CIV1'
    rival, faction = next((ptr,pol) for _,ptr,pol,_ in cities(peer) if pol not in (home,partner))
    rows = command(peer,'coop.civ')
    target = int(re.search(rf'veh #\d+ ([0-9A-F]+) pol={partner:08X} ',rows,re.I)[1],16)
    # Resolve ASLR using the active build's verified vehicle vtable slot.
    base_delta = u32(peer,u32(peer,target+0x508)+0x18) - 0x00CAB6F0
    prior = set(turret_list(peer,rival))
    dismiss_registration(peer)
    dismiss_service_error(peer)
    for wid in ('0445C99D','062EF353'): button(peer,wid)
    assert mem(peer,base_delta+0xBE39A0,5) == bytes.fromhex('83ec445657')
    command(peer,f'call {base_delta+0xBE39A0:#x} thiscall {rival:#x}')
    placed = set(turret_list(peer,rival)) - prior
    assert len(placed) == 1, ('No free wall slot in the disposable rival city',placed)
    turret = placed.pop()
    assert u32(peer,turret+0x688) == rival
    assert u32(peer,turret+0x588) == base_delta+0x01466C28
    assert u32(peer,u32(peer,turret+0x588)+0x18) == base_delta+0xBD0660
    vanilla = shoot(peer,turret,target,faction)
    command(peer,'coop.host 47812 TurretHost')
    assert 'Reserved founders: 4,' in command(peer,'coop.status')
    command(peer,'wait.frames 100 20000')
    guest = Console(args.guest) if args.guest else None
    mirror = None
    if guest:
        assert 'duoqa' in command(guest,'coop.savedb').lower()
        command(guest,'coop.join 127.0.0.1:47812 TurretGuest')
        wait_until(lambda: 'Civ' in command(guest,'game.mode') and
                   'Host campaign rules: 4 reserved founders' in command(guest,'coop.status'),
                   'Guest did not enter four-seat campaign',100)
        vehicle_id = re.search(rf'veh #(\d+) 0*{target:X} ',command(peer,'coop.civ'),re.I)[1]
        def target_mirror():
            row = re.search(rf'veh #{vehicle_id} ([0-9A-F]+) ',command(guest,'coop.civ'),re.I)
            return int(row[1],16) if row else None
        mirror = wait_until(target_mirror,'Missing founder vehicle on guest',20)
    hosted = shoot(peer,turret,target,faction)
    print('Native rival turret hits', vanilla, hosted, 'owner', hex(faction),
          'base max HP',health(peer,turret,0x5B4),flush=True)
    assert vanilla > 0 and abs(hosted-vanilla*1.15) < .55, (vanilla,hosted)
    if guest:
        wait_until(lambda:abs(health(guest,mirror,0x540)-health(peer,target,0x540)) < 1,
                   'Guest did not receive the turret hit',15)
        assert 'total faults: 0' in command(guest,'exc')
        print('PASS guest mirrored the damaged founder vehicle',flush=True)
    assert 'total faults: 0' in command(peer,'exc')


if __name__ == '__main__':
    main()
