import { asset } from '@kit.AssetStoreKit';
import { util } from '@kit.ArkTS';
import { RepositoryCredentials } from './GitRepositoryService';
import {
  credentialAlias,
  credentialTarget
} from './CredentialSecurity';
import {
  CredentialStore,
  MemoryCredentialStore
} from './CredentialStore';

export class HarmonyCredentialStore implements CredentialStore {
  private readonly memory: MemoryCredentialStore =
    new MemoryCredentialStore();

  async load(remoteUrl: string): Promise<RepositoryCredentials | undefined> {
    const target: string = credentialTarget(remoteUrl);
    const cached: RepositoryCredentials | undefined =
      await this.memory.load(target);
    if (cached !== undefined) {
      return cached;
    }
    try {
      const query: asset.AssetMap = this.aliasQuery(target);
      query.set(asset.Tag.RETURN_TYPE, asset.ReturnType.ALL);
      const results: Array<asset.AssetMap> = await asset.query(query);
      for (const result of results) {
        const storedTarget: string | undefined =
          this.decodeBytes(result.get(asset.Tag.DATA_LABEL_CRITICAL_1));
        if (storedTarget !== target) {
          continue;
        }
        const credentials: RepositoryCredentials | undefined =
          this.decodeCredentials(result.get(asset.Tag.SECRET));
        if (credentials !== undefined) {
          await this.memory.save(target, credentials);
          return credentials;
        }
      }
    } catch (_error) {
      // AssetStoreKit can be unavailable in a simulator or restricted device.
    }
    return undefined;
  }

  async save(
    remoteUrl: string,
    credentials: RepositoryCredentials
  ): Promise<void> {
    const target: string = credentialTarget(remoteUrl);
    await this.memory.save(target, credentials);
    try {
      const attributes: asset.AssetMap = this.aliasQuery(target);
      attributes.set(
        asset.Tag.SECRET,
        this.encode(this.serializeCredentials(credentials))
      );
      attributes.set(asset.Tag.ACCESSIBILITY,
        asset.Accessibility.DEVICE_FIRST_UNLOCKED);
      attributes.set(asset.Tag.AUTH_TYPE, asset.AuthType.NONE);
      attributes.set(asset.Tag.SYNC_TYPE, asset.SyncType.NEVER);
      attributes.set(asset.Tag.CONFLICT_RESOLUTION,
        asset.ConflictResolution.OVERWRITE);
      attributes.set(asset.Tag.DATA_LABEL_CRITICAL_1, this.encode(target));
      await asset.add(attributes);
    } catch (_error) {
      // The in-memory copy remains available for the current process.
    }
  }

  async remove(remoteUrl: string): Promise<void> {
    const target: string = credentialTarget(remoteUrl);
    await this.memory.remove(target);
    try {
      await asset.remove(this.aliasQuery(target));
    } catch (_error) {
      // Missing or unavailable secure assets are already absent from memory.
    }
  }

  private aliasQuery(target: string): asset.AssetMap {
    const query: asset.AssetMap = new Map();
    query.set(
      asset.Tag.ALIAS,
      this.encode(credentialAlias(target))
    );
    return query;
  }

  private encode(value: string): Uint8Array {
    return util.TextEncoder.create().encodeInto(value);
  }

  private decodeBytes(value: asset.Value | undefined): string | undefined {
    if (!(value instanceof Uint8Array)) {
      return undefined;
    }
    return util.TextDecoder.create().decodeToString(value);
  }

  private serializeCredentials(credentials: RepositoryCredentials): string {
    return `${credentials.username.length}:${credentials.username}` +
      credentials.password;
  }

  private decodeCredentials(
    value: asset.Value | undefined
  ): RepositoryCredentials | undefined {
    const serialized: string | undefined = this.decodeBytes(value);
    if (serialized === undefined) {
      return undefined;
    }
    const separator: number = serialized.indexOf(':');
    if (separator <= 0) {
      return undefined;
    }
    const usernameLength: number =
      Number(serialized.slice(0, separator));
    if (!Number.isInteger(usernameLength) || usernameLength < 0) {
      return undefined;
    }
    const usernameStart: number = separator + 1;
    const passwordStart: number = usernameStart + usernameLength;
    if (passwordStart > serialized.length) {
      return undefined;
    }
    return {
      username: serialized.slice(usernameStart, passwordStart),
      password: serialized.slice(passwordStart)
    };
  }
}
