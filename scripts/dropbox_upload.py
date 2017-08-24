#!/usr/bin/env python2

import dropbox, sys, os

d = os.getenv("TRAVIS_BUILD_ID")
if not d:
    d = "trash"

access_token = os.getenv("DROPBOX_TOKEN")

dbx = dropbox.Dropbox(access_token)

f = open(sys.argv[1])

fname = os.path.basename(sys.argv[1])

response = dbx.files_upload(f.read(), os.path.join("/", d, fname))
print 'uploaded: ', response

#print "=====================", fname, "======================"
#print client.share(fname)['url']
#print "=====================", len(fname) * "=", "======================"

