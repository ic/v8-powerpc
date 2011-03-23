# Copyright 2008 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


import test
import os
from os.path import join, exists


EXCLUDED = ['CVS']


FRAMEWORK = """
  browser.js
  shell.js
  jsref.js
  template.js
""".split()


TEST_DIRS = """
  ecma
  ecma_2
  ecma_3
  js1_1
  js1_2
  js1_3
  js1_4
  js1_5
""".split()


class MozillaTestCase(test.TestCase):

  def __init__(self, filename, path, context, root, mode, framework):
    super(MozillaTestCase, self).__init__(context, path, mode)
    self.filename = filename
    self.framework = framework
    self.root = root

  def IsNegative(self):
    return self.filename.endswith('-n.js')

  def GetLabel(self):
    return "%s mozilla %s" % (self.mode, self.GetName())

  def IsFailureOutput(self, output):
    if output.exit_code != 0:
      return True
    return 'FAILED!' in output.stdout

  def GetCommand(self):
    result = self.context.GetVmCommand(self, self.mode) + \
       [ '--expose-gc', join(self.root, 'mozilla-shell-emulation.js') ]
    result += self.framework
    result.append(self.filename)
    return result

  def GetName(self):
    return self.path[-1]

  def GetSource(self):
    return open(self.filename).read()


class MozillaTestConfiguration(test.TestConfiguration):

  def __init__(self, context, root):
    super(MozillaTestConfiguration, self).__init__(context, root)

  def ListTests(self, current_path, path, mode, variant_flags):
    tests = []
    for test_dir in TEST_DIRS:
      current_root = join(self.root, 'data', test_dir)
      for root, dirs, files in os.walk(current_root):
        for dotted in [x  for x in dirs if x.startswith('.')]:
          dirs.remove(dotted)
        for excluded in EXCLUDED:
          if excluded in dirs:
            dirs.remove(excluded)
        dirs.sort()
        root_path = root[len(self.root):].split(os.path.sep)
        root_path = current_path + [x for x in root_path if x]
        framework = []
        for i in xrange(len(root_path)):
          if i == 0: dir = root_path[1:]
          else: dir = root_path[1:-i]
          script = join(self.root, reduce(join, dir, ''), 'shell.js')
          if exists(script):
            framework.append(script)
        framework.reverse()
        files.sort()
        for file in files:
          if (not file in FRAMEWORK) and file.endswith('.js'):
            full_path = root_path + [file[:-3]]
            full_path = [x for x in full_path if x != 'data']
            if self.Contains(path, full_path):
              test = MozillaTestCase(join(root, file), full_path, self.context,
                                     self.root, mode, framework)
              tests.append(test)
    return tests

  def GetBuildRequirements(self):
    return ['sample', 'sample=shell']

  def GetTestStatus(self, sections, defs):
    status_file = join(self.root, 'mozilla.status')
    if exists(status_file):
      test.ReadConfigurationInto(status_file, sections, defs)


def GetConfiguration(context, root):
  return MozillaTestConfiguration(context, root)
