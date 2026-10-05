Import("env")

def before_upload(source, target, env):
    print("=== Preparing filesystem upload ===")
    print(f"Filesystem will be uploaded to: {env['UPLOAD_PORT']}")

env.AddPreAction("uploadfs", before_upload)