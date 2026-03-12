from sqlalchemy import Text, create_engine, Column, Integer, String, ForeignKey, Table, UniqueConstraint
from sqlalchemy.orm import declarative_base, relationship, sessionmaker, Session

Base = declarative_base()

network_packages = Table('network_packages', Base.metadata,
    Column('network_id', Integer, ForeignKey('networks.id')),
    Column('package_id', Integer, ForeignKey('packages.id'))
)

class ComputerPackage(Base):
    __tablename__ = 'computer_packages'
    computer_id = Column(Integer, ForeignKey('computers.id'), primary_key=True)
    package_id = Column(Integer, ForeignKey('packages.id'), primary_key=True)
    
    # This is where your health report lives
    health_report = Column(Text, nullable=True) 

    # Relationships to the parent objects
    computer = relationship("Computer", back_populates="package_associations")
    package = relationship("Package", back_populates="computer_associations")

class Network(Base):
    __tablename__ = "networks"
    id = Column(Integer, primary_key=True, index=True)
    name = Column(String(255), unique=True, index=True)
    ip = Column(String(255))
    mask = Column(String(255))

    __table_args__ = (
        UniqueConstraint('ip', 'mask', name='_ip_mask_uc'),
    )
    computers = relationship("Computer", back_populates="network")
    packages = relationship("Package", secondary=network_packages, back_populates="networks")

class Computer(Base):
    __tablename__ = "computers"
    id = Column(Integer, primary_key=True, index=True)
    pc_username = Column(String(255), unique=True, index=True)
    mac_address = Column(String(255), unique=True)
    network_id = Column(Integer, ForeignKey("networks.id"))

    network = relationship("Network", back_populates="computers")    
    package_associations = relationship("ComputerPackage", back_populates="computer")

class Package(Base):
    __tablename__ = "packages"
    id = Column(Integer, primary_key=True, index=True)
    name = Column(String(255))
    version = Column(String(50))
    arch = Column(String(50))
    metadata_json = Column(Text)

    __table_args__ = (
        UniqueConstraint('name', 'version', 'arch', name='_pkg_ver_arch_uc'),
    )
    networks = relationship("Network", secondary=network_packages, back_populates="packages")
    computer_associations = relationship("ComputerPackage", back_populates="package")

