import { RepositoryCredentials } from './GitRepositoryService';
import { credentialTarget } from './CredentialSecurity';

export interface CredentialStore {
  load(remoteUrl: string): Promise<RepositoryCredentials | undefined>;
  save(remoteUrl: string, credentials: RepositoryCredentials): Promise<void>;
  remove(remoteUrl: string): Promise<void>;
}

export class MemoryCredentialStore implements CredentialStore {
  private readonly values: Map<string, RepositoryCredentials> =
    new Map<string, RepositoryCredentials>();

  async load(remoteUrl: string): Promise<RepositoryCredentials | undefined> {
    const value: RepositoryCredentials | undefined =
      this.values.get(credentialTarget(remoteUrl));
    return value === undefined ? undefined : copyCredentials(value);
  }

  async save(
    remoteUrl: string,
    credentials: RepositoryCredentials
  ): Promise<void> {
    this.values.set(
      credentialTarget(remoteUrl),
      copyCredentials(credentials)
    );
  }

  async remove(remoteUrl: string): Promise<void> {
    this.values.delete(credentialTarget(remoteUrl));
  }
}

function copyCredentials(
  credentials: RepositoryCredentials
): RepositoryCredentials {
  return {
    username: credentials.username,
    password: credentials.password
  };
}
