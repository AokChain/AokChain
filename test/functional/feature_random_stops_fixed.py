#!/usr/bin/env python3
# Copyright (c) 2026 The AokChain Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""feature_random_stops.py with a fixed seed and 22 rounds: the same rounds at every run, with
every kind of stop among them and each of the four databases lost once. (The other run takes
its seed from the clock and makes 12 rounds; what kinds it meets is a matter of chance.)"""

from feature_random_stops import RandomStopsTest


class RandomStopsFixedTest(RandomStopsTest):
    def add_options(self, parser):
        super().add_options(parser)
        parser.set_defaults(seed=20261118, rounds=22)


if __name__ == '__main__':
    RandomStopsFixedTest().main()
